// Copyright UE CLI. All rights reserved.

#include "Niagara/UECliNiagaraOps.h"

#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ScopedTransaction.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#define LOCTEXT_NAMESPACE "UECliNiagaraOps"

namespace UECli::NiagaraOps
{
	namespace
	{
		struct FStage
		{
			const TCHAR* Name;
			ENiagaraScriptUsage Usage;
		};

		const FStage Stages[] = {
			{ TEXT("emitter-spawn"), ENiagaraScriptUsage::EmitterSpawnScript },
			{ TEXT("emitter-update"), ENiagaraScriptUsage::EmitterUpdateScript },
			{ TEXT("particle-spawn"), ENiagaraScriptUsage::ParticleSpawnScript },
			{ TEXT("particle-update"), ENiagaraScriptUsage::ParticleUpdateScript },
		};

		bool ParseStage(const FString& Name, ENiagaraScriptUsage& OutUsage, FString& OutError)
		{
			for (const FStage& Stage : Stages)
			{
				if (Name.Equals(Stage.Name, ESearchCase::IgnoreCase))
				{
					OutUsage = Stage.Usage;
					return true;
				}
			}
			OutError = FString::Printf(TEXT("Unknown stage '%s' (emitter-spawn | emitter-update | particle-spawn | particle-update)."), *Name);
			return false;
		}

		UNiagaraSystem* LoadSystem(const FString& Path, FString& OutError)
		{
			FString ObjectPath = Path;
			if (!ObjectPath.Contains(TEXT(".")))
			{
				ObjectPath += TEXT(".") + FPackageName::GetShortName(Path);
			}
			UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!System)
			{
				OutError = FString::Printf(TEXT("No Niagara system at '%s'."), *Path);
			}
			return System;
		}

		FNiagaraEmitterHandle* FindHandle(UNiagaraSystem& System, const FString& Emitter, FString& OutError)
		{
			for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			{
				if (Handle.GetName().ToString().Equals(Emitter, ESearchCase::IgnoreCase) || Handle.GetUniqueInstanceName().Equals(Emitter, ESearchCase::IgnoreCase))
				{
					return &Handle;
				}
			}
			TArray<FString> Names;
			for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			{
				Names.Add(Handle.GetName().ToString());
			}
			OutError = FString::Printf(TEXT("'%s' has no emitter '%s' (emitters: %s)."), *System.GetName(), *Emitter, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}

		UNiagaraGraph* EmitterGraph(const FNiagaraEmitterHandle& Handle)
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			const UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			return Source ? Source->NodeGraph : nullptr;
		}

		/** The parameter-map pin of a stack node in the given direction. */
		UEdGraphPin* MapPin(UEdGraphNode& Node, EEdGraphPinDirection Direction)
		{
			for (UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin && Pin->Direction == Direction && Pin->PinType.PinSubCategoryObject == FNiagaraTypeDefinition::GetParameterMapStruct())
				{
					return Pin;
				}
			}
			return nullptr;
		}

		/** Modules of one stage in execution order (walking the parameter-map chain back from the output node). */
		TArray<UNiagaraNodeFunctionCall*> StageModules(UNiagaraNodeOutput& Output)
		{
			TArray<UNiagaraNodeFunctionCall*> Modules;
			UEdGraphNode* Current = &Output;
			for (int32 Guard = 0; Current && Guard < 1000; ++Guard)
			{
				UEdGraphPin* In = MapPin(*Current, EGPD_Input);
				if (!In || In->LinkedTo.Num() == 0)
				{
					break;
				}
				UEdGraphNode* Previous = In->LinkedTo[0]->GetOwningNode();
				if (UNiagaraNodeFunctionCall* Module = Cast<UNiagaraNodeFunctionCall>(Previous))
				{
					Modules.Insert(Module, 0);
				}
				Current = Previous; // override (map set) nodes are walked through too
			}
			return Modules;
		}

		UNiagaraNodeOutput* StageOutput(const FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage, FString& OutError)
		{
			UNiagaraGraph* Graph = EmitterGraph(Handle);
			UNiagaraNodeOutput* Output = Graph ? Graph->FindEquivalentOutputNode(Usage) : nullptr;
			if (!Output)
			{
				OutError = Graph ? TEXT("The emitter has no such stage.") : TEXT("The emitter has no editable graph (stateless / lightweight emitter?).");
			}
			return Output;
		}

		UNiagaraNodeFunctionCall* FindModule(UNiagaraNodeOutput& Output, const FString& Module, FString& OutError)
		{
			const TArray<UNiagaraNodeFunctionCall*> Modules = StageModules(Output);
			for (UNiagaraNodeFunctionCall* Node : Modules)
			{
				if (Node->GetFunctionName().Equals(Module, ESearchCase::IgnoreCase))
				{
					return Node;
				}
			}
			TArray<FString> Names;
			for (const UNiagaraNodeFunctionCall* Node : Modules)
			{
				Names.Add(Node->GetFunctionName());
			}
			OutError = FString::Printf(TEXT("No module '%s' in this stage (modules: %s)."), *Module, *FString::Join(Names, TEXT(", ")));
			return nullptr;
		}

		/** The parameter-map set node feeding a function call's overrides, if any. */
		UEdGraphNode* OverrideNode(UNiagaraNodeFunctionCall& Function)
		{
			UEdGraphPin* In = MapPin(Function, EGPD_Input);
			UEdGraphNode* Previous = In && In->LinkedTo.Num() > 0 ? In->LinkedTo[0]->GetOwningNode() : nullptr;
			return Previous && Previous->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapSet") ? Previous : nullptr;
		}

		/** Dynamic-input function calls plugged into a function call's inputs, nested ones included. */
		void CollectDynamicInputs(UNiagaraNodeFunctionCall& Function, TArray<UNiagaraNodeFunctionCall*>& Out, int32 Depth = 0)
		{
			UEdGraphNode* Overrides = Depth < 16 ? OverrideNode(Function) : nullptr;
			if (!Overrides)
			{
				return;
			}
			for (UEdGraphPin* Pin : Overrides->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() == 0)
				{
					continue;
				}
				if (UNiagaraNodeFunctionCall* Dynamic = Cast<UNiagaraNodeFunctionCall>(Pin->LinkedTo[0]->GetOwningNode()); Dynamic && Dynamic != &Function)
				{
					if (!Out.Contains(Dynamic) && Pin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct())
					{
						Out.Add(Dynamic);
						CollectDynamicInputs(*Dynamic, Out, Depth + 1);
					}
				}
			}
		}

		/** Every node on a stage's parameter-map chain (modules, their override nodes, the input node). */
		TSet<UEdGraphNode*> ChainNodes(UNiagaraNodeOutput& Output)
		{
			TSet<UEdGraphNode*> Chain;
			UEdGraphNode* Current = &Output;
			for (int32 Guard = 0; Current && Guard < 1000; ++Guard)
			{
				Chain.Add(Current);
				UEdGraphPin* In = MapPin(*Current, EGPD_Input);
				Current = In && In->LinkedTo.Num() > 0 ? In->LinkedTo[0]->GetOwningNode() : nullptr;
			}
			return Chain;
		}

		/**
		 * A module with everything that belongs to it in the stack: its override node, dynamic inputs (nested
		 * ones too) and parameter reads. PreviousOutput is the map pin feeding the group, Downstream what the
		 * module's map output feeds.
		 */
		struct FModuleGroup
		{
			UEdGraphPin* PreviousOutput = nullptr;
			UEdGraphPin* Output = nullptr;
			TArray<UEdGraphPin*> Starts;     // inputs the previous output feeds
			TArray<UEdGraphPin*> Downstream; // inputs the module's output feeds
			TArray<UEdGraphNode*> Nodes;
		};

		bool GetModuleGroup(UNiagaraNodeOutput& StageOutput, UNiagaraNodeFunctionCall& Module, FModuleGroup& Out, FString& OutError)
		{
			UEdGraphNode* Start = OverrideNode(Module);
			UEdGraphPin* StartIn = MapPin(Start ? *Start : static_cast<UEdGraphNode&>(Module), EGPD_Input);
			Out.Output = MapPin(Module, EGPD_Output);
			if (!StartIn || StartIn->LinkedTo.Num() == 0 || !Out.Output)
			{
				OutError = TEXT("The module is not wired into the stack as expected.");
				return false;
			}
			Out.PreviousOutput = StartIn->LinkedTo[0];
			Out.Starts = Out.PreviousOutput->LinkedTo;
			Out.Downstream = Out.Output->LinkedTo;

			TSet<UEdGraphNode*> Chain = ChainNodes(StageOutput);
			Chain.Remove(&Module);
			if (Start)
			{
				Chain.Remove(Start);
			}
			TArray<UEdGraphNode*> Pending = { &Module };
			while (Pending.Num() > 0 && Out.Nodes.Num() < 500)
			{
				UEdGraphNode* Next = Pending.Pop();
				if (!Next || Chain.Contains(Next) || Out.Nodes.Contains(Next))
				{
					continue;
				}
				Out.Nodes.Add(Next);
				for (UEdGraphPin* Pin : Next->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input)
					{
						for (UEdGraphPin* Linked : Pin->LinkedTo)
						{
							Pending.Add(Linked->GetOwningNode());
						}
					}
				}
			}
			return true;
		}

		/** A stage module, or a dynamic input used by one of the stage's modules (for setting its inputs). */
		UNiagaraNodeFunctionCall* FindFunction(UNiagaraNodeOutput& Output, const FString& Name, FString& OutError)
		{
			FString Ignored;
			if (UNiagaraNodeFunctionCall* Module = FindModule(Output, Name, Ignored))
			{
				return Module;
			}
			TArray<UNiagaraNodeFunctionCall*> Dynamic;
			for (UNiagaraNodeFunctionCall* Module : StageModules(Output))
			{
				CollectDynamicInputs(*Module, Dynamic);
			}
			for (UNiagaraNodeFunctionCall* Node : Dynamic)
			{
				if (Node->GetFunctionName().Equals(Name, ESearchCase::IgnoreCase))
				{
					return Node;
				}
			}
			return FindModule(Output, Name, OutError);
		}

		/** Scripts holding the rapid-iteration values of a stage: the emitter's scripts or, for emitter stages, the system's. */
		TArray<UNiagaraScript*> AffectedScripts(UNiagaraSystem& System, const FNiagaraEmitterHandle& Handle, ENiagaraScriptUsage Usage)
		{
			TArray<UNiagaraScript*> Candidates;
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Data->GetScripts(Candidates, /*bCompilableOnly*/ false);
			}
			Candidates.Add(System.GetSystemSpawnScript());
			Candidates.Add(System.GetSystemUpdateScript());
			TArray<UNiagaraScript*> Result;
			for (UNiagaraScript* Script : Candidates)
			{
				if (Script && Script->ContainsUsage(Usage))
				{
					Result.AddUnique(Script);
				}
			}
			return Result;
		}

		/** Types whose values this file can encode / decode (parameter stores, user parameters). */
		bool IsValueType(const FNiagaraTypeDefinition& Type)
		{
			return Type == FNiagaraTypeDefinition::GetFloatDef() || Type == FNiagaraTypeDefinition::GetIntDef()
				|| Type == FNiagaraTypeDefinition::GetBoolDef() || Type == FNiagaraTypeDefinition::GetVec2Def()
				|| Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetVec4Def()
				|| Type == FNiagaraTypeDefinition::GetColorDef() || Type == FNiagaraTypeDefinition::GetPositionDef()
				|| Type == FNiagaraTypeDefinition::GetQuatDef();
		}

		/**
		 * Module inputs held as rapid-iteration constants. Like FNiagaraStackGraphUtilities::IsRapidIterationType,
		 * bools and enums are not: they can drive static switches, so their value lives on the override pin.
		 */
		bool IsRapidIterationType(const FNiagaraTypeDefinition& Type)
		{
			return IsValueType(Type) && Type != FNiagaraTypeDefinition::GetBoolDef();
		}

		/** Module inputs whose local value is the override pin's default string ("true" / enum value name). */
		bool IsPinValueType(const FNiagaraTypeDefinition& Type)
		{
			return Type == FNiagaraTypeDefinition::GetBoolDef() || Type.IsEnum();
		}

		/** Selectable display names of an enum input (hidden / _MAX entries left out; user enums have NewEnumeratorN internal names). */
		TArray<FString> EnumOptions(const UEnum& Enum)
		{
			TArray<FString> Names;
			for (int32 Index = 0; Index < Enum.NumEnums() - 1; ++Index)
			{
				if (!Enum.HasMetaData(TEXT("Hidden"), Index))
				{
					Names.Add(Enum.GetDisplayNameTextByIndex(Index).ToString());
				}
			}
			return Names;
		}

		int32 ComponentCount(const FNiagaraTypeDefinition& Type)
		{
			if (Type == FNiagaraTypeDefinition::GetVec2Def()) return 2;
			if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef()) return 3;
			if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetColorDef() || Type == FNiagaraTypeDefinition::GetQuatDef()) return 4;
			return 1;
		}

		TSharedPtr<FJsonValue> ValueToJson(const FNiagaraTypeDefinition& Type, const uint8* Data)
		{
			if (Type == FNiagaraTypeDefinition::GetFloatDef())
			{
				return MakeShared<FJsonValueNumber>(*reinterpret_cast<const float*>(Data));
			}
			if (Type == FNiagaraTypeDefinition::GetIntDef())
			{
				return MakeShared<FJsonValueNumber>(*reinterpret_cast<const int32*>(Data));
			}
			if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				return MakeShared<FJsonValueBoolean>(reinterpret_cast<const FNiagaraBool*>(Data)->GetValue());
			}
			TArray<TSharedPtr<FJsonValue>> Components;
			const float* Floats = reinterpret_cast<const float*>(Data);
			for (int32 Index = 0; Index < ComponentCount(Type); ++Index)
			{
				Components.Add(MakeShared<FJsonValueNumber>(Floats[Index]));
			}
			return MakeShared<FJsonValueArray>(Components);
		}

		bool JsonToValue(const FNiagaraTypeDefinition& Type, const TSharedPtr<FJsonValue>& Value, TArray<uint8>& OutBytes, FString& OutError)
		{
			OutBytes.SetNumZeroed(Type.GetSize());
			if (!Value.IsValid())
			{
				OutError = TEXT("value is required.");
				return false;
			}
			if (Type == FNiagaraTypeDefinition::GetFloatDef() || Type == FNiagaraTypeDefinition::GetIntDef())
			{
				double Number = 0;
				if (!Value->TryGetNumber(Number))
				{
					OutError = FString::Printf(TEXT("%s needs a number."), *Type.GetName());
					return false;
				}
				if (Type == FNiagaraTypeDefinition::GetFloatDef())
				{
					*reinterpret_cast<float*>(OutBytes.GetData()) = static_cast<float>(Number);
				}
				else
				{
					*reinterpret_cast<int32*>(OutBytes.GetData()) = static_cast<int32>(Number);
				}
				return true;
			}
			if (Type == FNiagaraTypeDefinition::GetBoolDef())
			{
				bool bValue = false;
				if (!Value->TryGetBool(bValue))
				{
					OutError = TEXT("bool needs true or false.");
					return false;
				}
				reinterpret_cast<FNiagaraBool*>(OutBytes.GetData())->SetValue(bValue);
				return true;
			}
			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			if (!Value->TryGetArray(Components) || Components->Num() != ComponentCount(Type))
			{
				OutError = FString::Printf(TEXT("%s needs an array of %d numbers."), *Type.GetName(), ComponentCount(Type));
				return false;
			}
			float* Floats = reinterpret_cast<float*>(OutBytes.GetData());
			for (int32 Index = 0; Index < Components->Num(); ++Index)
			{
				double Number = 0;
				if (!(*Components)[Index]->TryGetNumber(Number))
				{
					OutError = TEXT("Every component must be a number.");
					return false;
				}
				Floats[Index] = static_cast<float>(Number);
			}
			return true;
		}

		/** The override (parameter-map set) pin for an input, if the stack created one: linked = driven by a dynamic input / link. */
		UEdGraphPin* OverridePin(UNiagaraNodeFunctionCall& Module, const FName& AliasedName)
		{
			UEdGraphPin* In = MapPin(Module, EGPD_Input);
			UEdGraphNode* Previous = In && In->LinkedTo.Num() > 0 ? In->LinkedTo[0]->GetOwningNode() : nullptr;
			if (!Previous || Previous->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapSet"))
			{
				return nullptr;
			}
			for (UEdGraphPin* Pin : Previous->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == AliasedName)
				{
					return Pin;
				}
			}
			return nullptr;
		}

		/**
		 * The module's own default for an input: the default-value pin of the parameter-map get that reads
		 * Module.X inside the module graph (what the stack editor shows when nothing is overridden). Null when
		 * that default is itself computed (pin linked) or the format is not understood.
		 */
		TSharedPtr<FJsonValue> ModuleDefault(const UNiagaraNodeFunctionCall& Module, const FNiagaraVariable& Variable)
		{
			const UNiagaraGraph* Called = Module.GetCalledGraph();
			if (!Called)
			{
				return nullptr;
			}
			const FName Name = Variable.GetName();
			for (const UEdGraphNode* Node : Called->Nodes)
			{
				if (!Node || Node->GetClass()->GetName() != TEXT("NiagaraNodeParameterMapGet"))
				{
					continue;
				}
				// UNiagaraNodeParameterMapGet::GetDefaultPin is not exported; its output -> default-pin map is a UPROPERTY.
				const FMapProperty* MapProperty = FindFProperty<FMapProperty>(Node->GetClass(), TEXT("PinOutputToPinDefaultPersistentId"));
				const UEdGraphPin* const* OutputPin = Node->Pins.FindByPredicate([&Name](const UEdGraphPin* P) { return P && P->Direction == EGPD_Output && P->PinName == Name; });
				if (!MapProperty || !OutputPin)
				{
					continue;
				}
				FScriptMapHelper Map(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Node));
				const uint8* DefaultGuid = Map.FindValueFromHash(&(*OutputPin)->PersistentGuid);
				if (!DefaultGuid)
				{
					continue;
				}
				const FGuid Wanted = *reinterpret_cast<const FGuid*>(DefaultGuid);
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input || Pin->PersistentGuid != Wanted || Pin->LinkedTo.Num() > 0)
					{
						continue;
					}
					const FNiagaraTypeDefinition Type = Variable.GetType();
					const FString Text = Pin->DefaultValue.TrimStartAndEnd();
					if (Text.IsEmpty())
					{
						return nullptr;
					}
					if (Type == FNiagaraTypeDefinition::GetBoolDef())
					{
						return MakeShared<FJsonValueBoolean>(Text.ToBool());
					}
					if (const UEnum* Enum = Type.GetEnum())
					{
						const int64 Value = Enum->GetValueByNameString(Text);
						if (Value == INDEX_NONE)
						{
							return nullptr;
						}
						return MakeShared<FJsonValueString>(Enum->GetDisplayNameTextByValue(Value).ToString());
					}
					if (!IsValueType(Type))
					{
						return nullptr;
					}
					// Numbers in the pin string: "1.5", "1,2,3", "(R=1,G=1,B=1,A=1)".
					TArray<double> Numbers;
					FString Current;
					auto Flush = [&]()
					{
						if (!Current.IsEmpty() && Current != TEXT("-") && Current != TEXT("."))
						{
							Numbers.Add(FCString::Atod(*Current));
						}
						Current.Reset();
					};
					for (const TCHAR C : Text)
					{
						if (FChar::IsDigit(C) || C == '-' || C == '.' || ((C == 'e' || C == 'E') && !Current.IsEmpty()))
						{
							Current.AppendChar(C);
						}
						else
						{
							Flush();
						}
					}
					Flush();
					const int32 Count = ComponentCount(Type);
					if (Numbers.Num() != Count)
					{
						return nullptr;
					}
					if (Count == 1)
					{
						return MakeShared<FJsonValueNumber>(Numbers[0]);
					}
					TArray<TSharedPtr<FJsonValue>> Components;
					for (const double Number : Numbers)
					{
						Components.Add(MakeShared<FJsonValueNumber>(Number));
					}
					return MakeShared<FJsonValueArray>(Components);
				}
			}
			return nullptr;
		}

		struct FInputRef
		{
			FNiagaraVariable ModuleInput;   // "Module.X"
			FName AliasedName;              // "<FunctionName>.X"
			FNiagaraVariable RapidIteration; // "Constants.<Emitter>.<FunctionName>.X"
		};

		TArray<FInputRef> ModuleInputs(const FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Module, ENiagaraScriptUsage Usage)
		{
			TArray<FNiagaraVariable> Variables;
			FCompileConstantResolver Resolver(Handle.GetInstance(), Usage);
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(Module, Variables, Resolver,
				FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);

			TArray<FInputRef> Inputs;
			for (const FNiagaraVariable& Variable : Variables)
			{
				FInputRef Ref;
				Ref.ModuleInput = Variable;
				Ref.AliasedName = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(Variable.GetName()), &Module).GetParameterHandleString();
				Ref.RapidIteration = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
					FNiagaraVariable(Variable.GetType(), Ref.AliasedName), *Handle.GetUniqueInstanceName(), Usage);
				Inputs.Add(Ref);
			}
			return Inputs;
		}

		FString ShortInputName(const FNiagaraVariable& Variable)
		{
			FString Name = Variable.GetName().ToString();
			Name.RemoveFromStart(TEXT("Module."));
			return Name;
		}

		TSharedRef<FJsonObject> InputToJson(UNiagaraSystem& System, const FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Module,
			ENiagaraScriptUsage Usage, const FInputRef& Input)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			const FNiagaraTypeDefinition Type = Input.ModuleInput.GetType();
			Json->SetStringField(TEXT("name"), ShortInputName(Input.ModuleInput));
			Json->SetStringField(TEXT("type"), Type.GetName());
			const UEdGraphPin* Override = OverridePin(Module, Input.AliasedName);
			const bool bLinked = Override && Override->LinkedTo.Num() > 0;
			Json->SetBoolField(TEXT("linked"), bLinked);
			if (bLinked)
			{
				if (const UNiagaraNodeFunctionCall* Dynamic = Cast<UNiagaraNodeFunctionCall>(Override->LinkedTo[0]->GetOwningNode()))
				{
					Json->SetStringField(TEXT("dynamicInput"), Dynamic->GetFunctionName());
				}
				else if (Override->LinkedTo[0]->GetOwningNode()->GetClass()->GetName() == TEXT("NiagaraNodeParameterMapGet"))
				{
					Json->SetStringField(TEXT("linkedParameter"), Override->LinkedTo[0]->PinName.ToString());
				}
			}
			Json->SetBoolField(TEXT("settable"), (IsRapidIterationType(Type) || IsPinValueType(Type)) && !bLinked);
			if (const UEnum* Enum = Type.GetEnum())
			{
				TArray<TSharedPtr<FJsonValue>> Options;
				for (const FString& Option : EnumOptions(*Enum))
				{
					Options.Add(MakeShared<FJsonValueString>(Option));
				}
				Json->SetArrayField(TEXT("options"), Options);
			}
			if (IsPinValueType(Type))
			{
				if (Override && !bLinked && !Override->DefaultValue.IsEmpty())
				{
					if (Type == FNiagaraTypeDefinition::GetBoolDef())
					{
						Json->SetBoolField(TEXT("value"), Override->DefaultValue.ToBool());
					}
					else
					{
						const UEnum* Enum = Type.GetEnum();
						const int64 EnumValue = Enum ? Enum->GetValueByNameString(Override->DefaultValue) : INDEX_NONE;
						Json->SetStringField(TEXT("value"), EnumValue != INDEX_NONE ? Enum->GetDisplayNameTextByValue(EnumValue).ToString() : Override->DefaultValue);
					}
				}
			}
			else if (IsRapidIterationType(Type))
			{
				for (UNiagaraScript* Script : AffectedScripts(System, Handle, Usage))
				{
					if (const uint8* Data = Script->RapidIterationParameters.GetParameterData(Input.RapidIteration))
					{
						Json->SetField(TEXT("value"), ValueToJson(Type, Data));
						break;
					}
				}
			}
			else if (Override && !bLinked && !Override->DefaultValue.IsEmpty())
			{
				Json->SetStringField(TEXT("value"), Override->DefaultValue);
			}
			if (const TSharedPtr<FJsonValue> Default = ModuleDefault(Module, Input.ModuleInput))
			{
				Json->SetField(TEXT("default"), Default);
			}
			return Json;
		}

		TSharedRef<FJsonObject> ModuleToJson(UNiagaraSystem& System, const FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Module,
			ENiagaraScriptUsage Usage, int32 Index, int32 Depth = 0)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Module.GetFunctionName());
			Json->SetStringField(TEXT("script"), Module.FunctionScript ? Module.FunctionScript->GetPathName() : FString());
			Json->SetBoolField(TEXT("enabled"), Module.GetDesiredEnabledState() != ENodeEnabledState::Disabled);
			Json->SetNumberField(TEXT("index"), Index);
			TArray<TSharedPtr<FJsonValue>> Inputs;
			for (const FInputRef& Input : ModuleInputs(Handle, Module, Usage))
			{
				Inputs.Add(MakeShared<FJsonValueObject>(InputToJson(System, Handle, Module, Usage, Input)));
			}
			Json->SetArrayField(TEXT("inputs"), Inputs);

			// Dynamic inputs plugged directly into this function's inputs (their own ones nest inside).
			TArray<TSharedPtr<FJsonValue>> Dynamic;
			if (UEdGraphNode* Overrides = Depth < 8 ? OverrideNode(Module) : nullptr)
			{
				for (UEdGraphPin* Pin : Overrides->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0 && Pin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct())
					{
						if (UNiagaraNodeFunctionCall* Child = Cast<UNiagaraNodeFunctionCall>(Pin->LinkedTo[0]->GetOwningNode()); Child && Child != &Module)
						{
							Dynamic.Add(MakeShared<FJsonValueObject>(ModuleToJson(System, Handle, *Child, Usage, Dynamic.Num(), Depth + 1)));
						}
					}
				}
			}
			if (Dynamic.Num() > 0)
			{
				Json->SetArrayField(TEXT("dynamicInputs"), Dynamic);
			}
			return Json;
		}

		TSharedRef<FJsonObject> EmitterToJson(UNiagaraSystem& System, const FNiagaraEmitterHandle& Handle)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Handle.GetName().ToString());
			Json->SetStringField(TEXT("id"), Handle.GetId().ToString(EGuidFormats::DigitsWithHyphens));
			Json->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());

			TArray<TSharedPtr<FJsonValue>> Renderers;
			TArray<TSharedPtr<FJsonValue>> RendererPaths;
			TArray<TSharedPtr<FJsonValue>> StagesJson;
			if (const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Json->SetStringField(TEXT("simTarget"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("gpu") : TEXT("cpu"));
				Json->SetBoolField(TEXT("localSpace"), Data->bLocalSpace);
				Json->SetBoolField(TEXT("determinism"), Data->bDeterminism);
				for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
				{
					if (Renderer)
					{
						Renderers.Add(MakeShared<FJsonValueString>(Renderer->GetClass()->GetName()));
						RendererPaths.Add(MakeShared<FJsonValueString>(Renderer->GetPathName()));
					}
				}
				for (const FStage& Stage : Stages)
				{
					FString Ignored;
					UNiagaraNodeOutput* Output = StageOutput(Handle, Stage.Usage, Ignored);
					if (!Output)
					{
						continue;
					}
					const TSharedRef<FJsonObject> StageJson = MakeShared<FJsonObject>();
					StageJson->SetStringField(TEXT("stage"), Stage.Name);
					TArray<TSharedPtr<FJsonValue>> Modules;
					int32 Index = 0;
					for (UNiagaraNodeFunctionCall* Module : StageModules(*Output))
					{
						Modules.Add(MakeShared<FJsonValueObject>(ModuleToJson(System, Handle, *Module, Stage.Usage, Index++)));
					}
					StageJson->SetArrayField(TEXT("modules"), Modules);
					StagesJson.Add(MakeShared<FJsonValueObject>(StageJson));
				}
			}
			Json->SetArrayField(TEXT("renderers"), Renderers);
			Json->SetArrayField(TEXT("rendererPaths"), RendererPaths);
			Json->SetArrayField(TEXT("stages"), StagesJson);
			return Json;
		}

		void Changed(UNiagaraSystem& System, UNiagaraGraph* Graph)
		{
			if (Graph)
			{
				Graph->NotifyGraphChanged();
			}
			System.MarkPackageDirty();
			System.RequestCompile(false);
		}

		FString UserShortName(const FNiagaraVariableBase& Variable)
		{
			FString Name = Variable.GetName().ToString();
			Name.RemoveFromStart(TEXT("User."));
			return Name;
		}

		TSharedRef<FJsonObject> UserParameterToJson(const UNiagaraSystem& System, const FNiagaraVariable& Variable)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			const FNiagaraTypeDefinition Type = Variable.GetType();
			Json->SetStringField(TEXT("name"), UserShortName(Variable));
			Json->SetStringField(TEXT("type"), Type.GetName());
			if (IsValueType(Type))
			{
				if (const uint8* Data = System.GetExposedParameters().GetParameterData(Variable))
				{
					Json->SetField(TEXT("value"), ValueToJson(Type, Data));
				}
			}
			return Json;
		}

		bool UserType(const FString& Name, FNiagaraTypeDefinition& Out)
		{
			const TPair<const TCHAR*, FNiagaraTypeDefinition> Types[] = {
				{ TEXT("float"), FNiagaraTypeDefinition::GetFloatDef() },
				{ TEXT("int"), FNiagaraTypeDefinition::GetIntDef() },
				{ TEXT("bool"), FNiagaraTypeDefinition::GetBoolDef() },
				{ TEXT("vector2"), FNiagaraTypeDefinition::GetVec2Def() },
				{ TEXT("vector3"), FNiagaraTypeDefinition::GetVec3Def() },
				{ TEXT("vector4"), FNiagaraTypeDefinition::GetVec4Def() },
				{ TEXT("color"), FNiagaraTypeDefinition::GetColorDef() },
				{ TEXT("position"), FNiagaraTypeDefinition::GetPositionDef() },
			};
			for (const auto& Pair : Types)
			{
				if (Name.Equals(Pair.Key, ESearchCase::IgnoreCase))
				{
					Out = Pair.Value;
					return true;
				}
			}
			return false;
		}

		const FNiagaraVariable* FindUserParameter(const TArray<FNiagaraVariable>& Variables, const FString& Name)
		{
			return Variables.FindByPredicate([&Name](const FNiagaraVariable& V) { return UserShortName(V).Equals(Name, ESearchCase::IgnoreCase); });
		}

		/** Shared lookup for the module-level operations. */
		struct FModuleTarget
		{
			UNiagaraSystem* System = nullptr;
			FNiagaraEmitterHandle* Handle = nullptr;
			ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
			UNiagaraNodeOutput* Output = nullptr;
		};

		bool ResolveStage(const FString& Path, const FString& Emitter, const FString& Stage, FModuleTarget& Out, FString& OutError, bool& bOutNotFound)
		{
			bOutNotFound = true;
			Out.System = LoadSystem(Path, OutError);
			Out.Handle = Out.System ? FindHandle(*Out.System, Emitter, OutError) : nullptr;
			if (!Out.Handle)
			{
				return false;
			}
			bOutNotFound = false;
			if (!ParseStage(Stage, Out.Usage, OutError))
			{
				return false;
			}
			Out.Output = StageOutput(*Out.Handle, Out.Usage, OutError);
			return Out.Output != nullptr;
		}
	}

	bool Describe(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		bOutNotFound = System == nullptr;
		if (!System)
		{
			return false;
		}
		TArray<TSharedPtr<FJsonValue>> Emitters;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			Emitters.Add(MakeShared<FJsonValueObject>(EmitterToJson(*System, Handle)));
		}
		Out->SetStringField(TEXT("path"), System->GetPathName());
		Out->SetArrayField(TEXT("emitters"), Emitters);
		TArray<TSharedPtr<FJsonValue>> UserParameters;
		TArray<FNiagaraVariable> UserVariables;
		System->GetExposedParameters().GetUserParameters(UserVariables);
		for (const FNiagaraVariable& Variable : UserVariables)
		{
			UserParameters.Add(MakeShared<FJsonValueObject>(UserParameterToJson(*System, Variable)));
		}
		Out->SetArrayField(TEXT("userParameters"), UserParameters);
		return true;
	}

	bool SetUserParameter(const FString& Path, const FString& Name, const FString& TypeName, const TSharedPtr<FJsonValue>& Value,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		bOutNotFound = System == nullptr;
		if (!System)
		{
			return false;
		}
		FString Short = Name;
		Short.RemoveFromStart(TEXT("User."));
		if (Short.IsEmpty() || !FName::IsValidXName(Short, INVALID_OBJECTNAME_CHARACTERS))
		{
			OutError = FString::Printf(TEXT("'%s' is not a valid parameter name."), *Name);
			return false;
		}

		FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
		FNiagaraParameterStore& BaseStore = Store;
		TArray<FNiagaraVariable> Existing;
		Store.GetUserParameters(Existing);
		FNiagaraVariable Variable;
		if (const FNiagaraVariable* Found = FindUserParameter(Existing, Short))
		{
			Variable = *Found;
			FNiagaraTypeDefinition Requested;
			if (!TypeName.IsEmpty() && UserType(TypeName, Requested) && Requested != Variable.GetType())
			{
				OutError = FString::Printf(TEXT("User parameter '%s' already exists as %s."), *Short, *Variable.GetType().GetName());
				return false;
			}
		}
		else
		{
			FNiagaraTypeDefinition Type;
			if (!UserType(TypeName, Type))
			{
				OutError = FString::Printf(TEXT("New user parameter '%s' needs a type: float | int | bool | vector2 | vector3 | vector4 | color | position."), *Short);
				return false;
			}
			Variable = FNiagaraVariable(Type, FName(*(TEXT("User.") + Short)));
		}
		if (!IsValueType(Variable.GetType()))
		{
			OutError = FString::Printf(TEXT("User parameter '%s' is %s; only numeric, bool, vector and color values can be set here."), *Short, *Variable.GetType().GetName());
			return false;
		}
		TArray<uint8> Bytes;
		if (!JsonToValue(Variable.GetType(), Value, Bytes, OutError))
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetUserParameter", "UE CLI: Set Niagara User Parameter"));
		System->Modify();
		BaseStore.AddParameter(Variable, /*bInitialize*/ true); // no-op when it exists
		BaseStore.SetParameterData(Bytes.GetData(), Variable);
		System->MarkPackageDirty();
		Out = UserParameterToJson(*System, Variable);
		return true;
	}

	bool RemoveUserParameter(const FString& Path, const FString& Name, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		bOutNotFound = true;
		if (!System)
		{
			return false;
		}
		FString Short = Name;
		Short.RemoveFromStart(TEXT("User."));
		TArray<FNiagaraVariable> Existing;
		System->GetExposedParameters().GetUserParameters(Existing);
		const FNiagaraVariable* Found = FindUserParameter(Existing, Short);
		if (!Found)
		{
			OutError = FString::Printf(TEXT("'%s' has no user parameter '%s'."), *System->GetName(), *Short);
			return false;
		}
		bOutNotFound = false;
		const FScopedTransaction Transaction(LOCTEXT("RemoveUserParameter", "UE CLI: Remove Niagara User Parameter"));
		System->Modify();
		FNiagaraParameterStore& BaseStore = System->GetExposedParameters();
		BaseStore.RemoveParameter(*Found);
		Changed(*System, nullptr);
		return true;
	}

	bool CreateSystem(const FString& Path, const TArray<FString>& Emitters, TSharedRef<FJsonObject>& Out, FString& OutError)
	{
		FString PackagePath, AssetName;
		if (!Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || !Path.StartsWith(TEXT("/Game/")) || AssetName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("path must look like /Game/Folder/NS_Name (got '%s')."), *Path);
			return false;
		}
		AssetName.Split(TEXT("."), &AssetName, nullptr);
		FString Existing;
		if (LoadSystem(Path, Existing))
		{
			OutError = FString::Printf(TEXT("'%s' already exists."), *Path);
			return false;
		}
		for (const FString& Emitter : Emitters) // check them first so a bad one leaves no half-made asset behind
		{
			FString EmitterPath = Emitter;
			if (!EmitterPath.Contains(TEXT(".")))
			{
				EmitterPath += TEXT(".") + FPackageName::GetShortName(Emitter);
			}
			if (!LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				OutError = FString::Printf(TEXT("No Niagara emitter at '%s' (templates live under /Niagara/DefaultAssets/Templates/Emitters/)."), *Emitter);
				return false;
			}
		}
		// The factory class is not exported from NiagaraEditor; look it up by name.
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
		UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
		if (!Factory)
		{
			OutError = TEXT("The Niagara system factory is not available.");
			return false;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UNiagaraSystem* System = Cast<UNiagaraSystem>(AssetTools.CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), Factory));
		if (!System)
		{
			OutError = FString::Printf(TEXT("Could not create '%s'."), *Path);
			return false;
		}
		for (const FString& Emitter : Emitters)
		{
			TSharedRef<FJsonObject> Ignored = MakeShared<FJsonObject>();
			bool bNotFound = false;
			if (!AddEmitter(System->GetPathName(), Emitter, FString(), Ignored, OutError, bNotFound))
			{
				return false;
			}
		}
		bool bNotFound = false;
		return Describe(System->GetPathName(), Out, OutError, bNotFound);
	}

	bool AddEmitter(const FString& Path, const FString& EmitterAsset, const FString& Name, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		bOutNotFound = System == nullptr;
		if (!System)
		{
			return false;
		}
		FString AssetPath = EmitterAsset;
		if (!AssetPath.Contains(TEXT(".")))
		{
			AssetPath += TEXT(".") + FPackageName::GetShortName(EmitterAsset);
		}
		UNiagaraEmitter* Source = LoadObject<UNiagaraEmitter>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Source)
		{
			OutError = FString::Printf(TEXT("No Niagara emitter at '%s' (templates live under /Niagara/DefaultAssets/Templates/Emitters/)."), *EmitterAsset);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddEmitter", "UE CLI: Add Emitter"));
		System->Modify();
		const FGuid Id = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid, /*bCreateCopy*/ true);
		FNiagaraEmitterHandle* Added = System->GetEmitterHandles().FindByPredicate([&Id](const FNiagaraEmitterHandle& H) { return H.GetId() == Id; });
		if (!Added)
		{
			OutError = TEXT("The emitter could not be added.");
			return false;
		}
		if (!Name.IsEmpty())
		{
			Added->SetName(FName(*Name), *System);
		}
		Changed(*System, nullptr);
		Out = EmitterToJson(*System, *Added);
		return true;
	}

	bool RemoveEmitter(const FString& Path, const FString& Emitter, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter, OutError) : nullptr;
		bOutNotFound = Handle == nullptr;
		if (!Handle)
		{
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveEmitter", "UE CLI: Remove Emitter"));
		System->Modify();
		System->RemoveEmitterHandlesById({ Handle->GetId() });
		Changed(*System, nullptr);
		return true;
	}

	bool AddModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& ModuleScript, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		FString ScriptPath = ModuleScript;
		if (!ScriptPath.Contains(TEXT(".")))
		{
			ScriptPath += TEXT(".") + FPackageName::GetShortName(ModuleScript);
		}
		UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Script || Script->GetUsage() != ENiagaraScriptUsage::Module)
		{
			OutError = FString::Printf(TEXT("'%s' is not a Niagara module script (modules live under /Niagara/Modules/)."), *ModuleScript);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddModule", "UE CLI: Add Niagara Module"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		UNiagaraNodeFunctionCall* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Target.Output, Index < 0 ? INDEX_NONE : Index);
		if (!Module)
		{
			OutError = TEXT("The module could not be added to this stage.");
			return false;
		}
		Changed(*Target.System, Graph);
		const int32 Position = StageModules(*Target.Output).IndexOfByKey(Module);
		Out = ModuleToJson(*Target.System, *Target.Handle, *Module, Target.Usage, Position);
		return true;
	}

	bool RemoveModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindModule(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}

		// Unlink the module's group (override node, dynamic inputs) from the parameter-map chain and
		// reconnect its neighbours, which is what removing a module from the stack amounts to.
		FModuleGroup Group;
		if (!GetModuleGroup(*Target.Output, *Node, Group, OutError))
		{
			return false;
		}
		const TArray<UEdGraphNode*>& ToRemove = Group.Nodes;
		UEdGraphPin* Upstream = Group.PreviousOutput;
		const TArray<UEdGraphPin*> Downstream = Group.Downstream;

		const FScopedTransaction Transaction(LOCTEXT("RemoveModule", "UE CLI: Remove Niagara Module"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		for (UEdGraphNode* Removed : ToRemove)
		{
			Removed->Modify();
			Removed->BreakAllNodeLinks();
		}
		for (UEdGraphPin* Next : Downstream)
		{
			if (Next)
			{
				Next->GetOwningNode()->Modify();
				Upstream->MakeLinkTo(Next);
			}
		}
		for (UEdGraphNode* Removed : ToRemove)
		{
			Graph->RemoveNode(Removed);
		}
		Changed(*Target.System, Graph);
		return true;
	}

	bool MoveModule(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, int32 Index,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindModule(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}
		TArray<UNiagaraNodeFunctionCall*> Others = StageModules(*Target.Output);
		const int32 Current = Others.IndexOfByKey(Node);
		Others.Remove(Node);
		if (Index < 0 || Index > Others.Num())
		{
			OutError = FString::Printf(TEXT("index must be 0..%d."), Others.Num());
			return false;
		}
		FModuleGroup Group;
		if (!GetModuleGroup(*Target.Output, *Node, Group, OutError))
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("MoveModule", "UE CLI: Move Niagara Module"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		auto ModifyOwner = [](UEdGraphPin* Pin) { Pin->GetOwningNode()->Modify(); };

		if (Index != Current)
		{
			// Take the group out: previous output -> what the module fed.
			ModifyOwner(Group.PreviousOutput);
			ModifyOwner(Group.Output);
			Group.PreviousOutput->BreakAllPinLinks();
			Group.Output->BreakAllPinLinks();
			for (UEdGraphPin* Next : Group.Downstream)
			{
				ModifyOwner(Next);
				Group.PreviousOutput->MakeLinkTo(Next);
			}

			// Put it back in front of the module now at Index (or the stage output).
			UEdGraphNode* Before = Others.IsValidIndex(Index) ? static_cast<UEdGraphNode*>(Others[Index]) : Target.Output;
			if (UNiagaraNodeFunctionCall* BeforeModule = Cast<UNiagaraNodeFunctionCall>(Before))
			{
				if (UEdGraphNode* BeforeOverride = OverrideNode(*BeforeModule))
				{
					Before = BeforeOverride;
				}
			}
			UEdGraphPin* BeforeIn = MapPin(*Before, EGPD_Input);
			UEdGraphPin* Feed = BeforeIn && BeforeIn->LinkedTo.Num() > 0 ? BeforeIn->LinkedTo[0] : nullptr;
			if (!Feed)
			{
				OutError = TEXT("The target position is not wired as expected.");
				return false;
			}
			const TArray<UEdGraphPin*> NextStarts = Feed->LinkedTo;
			ModifyOwner(Feed);
			Feed->BreakAllPinLinks();
			for (UEdGraphPin* Start : Group.Starts)
			{
				ModifyOwner(Start);
				Feed->MakeLinkTo(Start);
			}
			for (UEdGraphPin* Next : NextStarts)
			{
				ModifyOwner(Next);
				Group.Output->MakeLinkTo(Next);
			}
			Changed(*Target.System, Graph);
		}
		Out = ModuleToJson(*Target.System, *Target.Handle, *Node, Target.Usage, StageModules(*Target.Output).IndexOfByKey(Node));
		return true;
	}

	bool SetModuleEnabled(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, bool bEnabled,
		FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindModule(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("EnableModule", "UE CLI: Enable / Disable Niagara Module"));
		Target.System->Modify();
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(*Node, bEnabled);
		Changed(*Target.System, Target.Output->GetNiagaraGraph());
		return true;
	}

	bool SetInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const TSharedPtr<FJsonValue>& Value, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindFunction(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}

		const TArray<FInputRef> Inputs = ModuleInputs(*Target.Handle, *Node, Target.Usage);
		const FInputRef* Ref = Inputs.FindByPredicate([&Input](const FInputRef& R) { return ShortInputName(R.ModuleInput).Equals(Input, ESearchCase::IgnoreCase); });
		if (!Ref)
		{
			TArray<FString> Names;
			for (const FInputRef& R : Inputs)
			{
				Names.Add(ShortInputName(R.ModuleInput));
			}
			OutError = FString::Printf(TEXT("Module '%s' has no input '%s' (inputs: %s)."), *Module, *Input, *FString::Join(Names, TEXT(", ")));
			bOutNotFound = true;
			return false;
		}
		const FNiagaraTypeDefinition Type = Ref->ModuleInput.GetType();
		if (!IsRapidIterationType(Type) && !IsPinValueType(Type))
		{
			OutError = FString::Printf(TEXT("Input '%s' is %s; only numeric, bool, enum, vector, color and quat inputs can be set here."), *Input, *Type.GetName());
			return false;
		}
		if (const UEdGraphPin* Override = OverridePin(*Node, Ref->AliasedName); Override && Override->LinkedTo.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Input '%s' is driven by a dynamic input or a linked parameter; reset it first (DELETE /niagara/module/input, uecli niagara reset-input)."), *Input);
			return false;
		}

		if (IsPinValueType(Type))
		{
			// Bools and enums: the value is the override pin's default string, like the stack editor writes it.
			FString PinValue;
			if (const UEnum* Enum = Type.GetEnum())
			{
				FString Name;
				double Number = 0;
				int64 EnumValue = INDEX_NONE;
				if (Value.IsValid() && Value->TryGetString(Name))
				{
					for (int32 Index = 0; Index < Enum->NumEnums() - 1 && EnumValue == INDEX_NONE; ++Index)
					{
						if (Enum->GetNameStringByIndex(Index).Equals(Name, ESearchCase::IgnoreCase) || Enum->GetDisplayNameTextByIndex(Index).ToString().Equals(Name, ESearchCase::IgnoreCase))
						{
							EnumValue = Enum->GetValueByIndex(Index);
						}
					}
				}
				else if (Value.IsValid() && Value->TryGetNumber(Number) && Enum->IsValidEnumValue(static_cast<int64>(Number)))
				{
					EnumValue = static_cast<int64>(Number);
				}
				if (EnumValue == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Input '%s' needs one of: %s."), *Input, *FString::Join(EnumOptions(*Enum), TEXT(", ")));
					return false;
				}
				PinValue = Enum->GetNameStringByValue(EnumValue);
			}
			else
			{
				bool bValue = false;
				if (!Value.IsValid() || !Value->TryGetBool(bValue))
				{
					OutError = TEXT("bool needs true or false.");
					return false;
				}
				PinValue = LexToString(bValue);
			}

			const FScopedTransaction Transaction(LOCTEXT("SetPinInput", "UE CLI: Set Niagara Module Input"));
			Target.System->Modify();
			UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
			Graph->Modify();
			UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*Node, FNiagaraParameterHandle(Ref->AliasedName), Type, FGuid(), FGuid());
			Pin.GetOwningNode()->Modify();
			Pin.DefaultValue = PinValue;
			Pin.GetOwningNode()->PinDefaultValueChanged(&Pin);
			Changed(*Target.System, Graph);
			Out = InputToJson(*Target.System, *Target.Handle, *Node, Target.Usage, *Ref);
			return true;
		}

		TArray<uint8> Bytes;
		if (!JsonToValue(Type, Value, Bytes, OutError))
		{
			return false;
		}

		const TArray<UNiagaraScript*> Scripts = AffectedScripts(*Target.System, *Target.Handle, Target.Usage);
		if (Scripts.Num() == 0)
		{
			OutError = TEXT("No compiled script holds this stage's values yet; compile the system first.");
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("SetInput", "UE CLI: Set Niagara Module Input"));
		Target.System->Modify();
		for (UNiagaraScript* Script : Scripts)
		{
			Script->Modify();
			Script->RapidIterationParameters.SetParameterData(Bytes.GetData(), Ref->RapidIteration, /*bAdd*/ true);
		}
		Changed(*Target.System, nullptr);
		Out = InputToJson(*Target.System, *Target.Handle, *Node, Target.Usage, *Ref);
		return true;
	}

	bool SetDynamicInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const FString& ScriptPath, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindFunction(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}
		const TArray<FInputRef> Inputs = ModuleInputs(*Target.Handle, *Node, Target.Usage);
		const FInputRef* Ref = Inputs.FindByPredicate([&Input](const FInputRef& R) { return ShortInputName(R.ModuleInput).Equals(Input, ESearchCase::IgnoreCase); });
		if (!Ref)
		{
			OutError = FString::Printf(TEXT("Module '%s' has no input '%s'."), *Module, *Input);
			bOutNotFound = true;
			return false;
		}
		if (const UEdGraphPin* Existing = OverridePin(*Node, Ref->AliasedName); Existing && Existing->LinkedTo.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Input '%s' already has a dynamic input or link; reset it first (DELETE /niagara/module/input, uecli niagara reset-input)."), *Input);
			return false;
		}

		FString ObjectPath = ScriptPath;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			ObjectPath += TEXT(".") + FPackageName::GetShortName(ScriptPath);
		}
		UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Script || Script->GetUsage() != ENiagaraScriptUsage::DynamicInput)
		{
			OutError = FString::Printf(TEXT("'%s' is not a Niagara dynamic input script (they live under /Niagara/DynamicInputs/)."), *ScriptPath);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetDynamicInput", "UE CLI: Set Niagara Dynamic Input"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*Node, FNiagaraParameterHandle(Ref->AliasedName), Ref->ModuleInput.GetType(), FGuid(), FGuid());
		UNiagaraNodeFunctionCall* Dynamic = nullptr;
		FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(Pin, Script, Dynamic);
		if (!Dynamic)
		{
			OutError = TEXT("The dynamic input could not be added.");
			return false;
		}
		// The local value is replaced: drop its rapid-iteration constant like the stack editor does.
		for (UNiagaraScript* Affected : AffectedScripts(*Target.System, *Target.Handle, Target.Usage))
		{
			Affected->Modify();
			Affected->RapidIterationParameters.RemoveParameter(Ref->RapidIteration);
		}
		Changed(*Target.System, Graph);
		Out = ModuleToJson(*Target.System, *Target.Handle, *Dynamic, Target.Usage, 0);
		return true;
	}

	bool LinkInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		const FString& Parameter, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindFunction(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}
		const TArray<FInputRef> Inputs = ModuleInputs(*Target.Handle, *Node, Target.Usage);
		const FInputRef* Ref = Inputs.FindByPredicate([&Input](const FInputRef& R) { return ShortInputName(R.ModuleInput).Equals(Input, ESearchCase::IgnoreCase); });
		if (!Ref)
		{
			OutError = FString::Printf(TEXT("Module '%s' has no input '%s'."), *Module, *Input);
			bOutNotFound = true;
			return false;
		}
		if (const UEdGraphPin* Existing = OverridePin(*Node, Ref->AliasedName); Existing && Existing->LinkedTo.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Input '%s' already has a dynamic input or link; reset it first (uecli niagara reset-input)."), *Input);
			return false;
		}

		// Only user parameters are offered: they exist in the system, so the link can be checked here.
		FString Short = Parameter;
		Short.RemoveFromStart(TEXT("User."));
		TArray<FNiagaraVariable> UserVariables;
		Target.System->GetExposedParameters().GetUserParameters(UserVariables);
		const FNiagaraVariable* User = FindUserParameter(UserVariables, Short);
		if (!User)
		{
			OutError = FString::Printf(TEXT("'%s' has no user parameter '%s'; create it with uecli niagara set-param."), *Target.System->GetName(), *Short);
			bOutNotFound = true;
			return false;
		}
		if (User->GetType() != Ref->ModuleInput.GetType())
		{
			OutError = FString::Printf(TEXT("User.%s is %s but input '%s' is %s."), *Short, *User->GetType().GetName(), *Input, *Ref->ModuleInput.GetType().GetName());
			return false;
		}
		// The redirection store's keys may lack the namespace; link to the fully qualified User.X,
		// otherwise the graph reads it as Module.X.
		auto Qualified = [](const FNiagaraVariable& Variable)
		{
			const FString Name = Variable.GetName().ToString();
			return FNiagaraVariableBase(Variable.GetType(), FName(*(Name.StartsWith(TEXT("User.")) ? Name : TEXT("User.") + Name)));
		};
		TSet<FNiagaraVariableBase> Known;
		for (const FNiagaraVariable& Variable : UserVariables)
		{
			Known.Add(Qualified(Variable));
		}

		const FScopedTransaction Transaction(LOCTEXT("LinkInput", "UE CLI: Link Niagara Module Input"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*Node, FNiagaraParameterHandle(Ref->AliasedName), Ref->ModuleInput.GetType(), FGuid(), FGuid());
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, Qualified(*User), Known);
		for (UNiagaraScript* Affected : AffectedScripts(*Target.System, *Target.Handle, Target.Usage))
		{
			Affected->Modify();
			Affected->RapidIterationParameters.RemoveParameter(Ref->RapidIteration);
		}
		Changed(*Target.System, Graph);
		Out = InputToJson(*Target.System, *Target.Handle, *Node, Target.Usage, *Ref);
		return true;
	}

	bool ResetInput(const FString& Path, const FString& Emitter, const FString& Stage, const FString& Module, const FString& Input,
		TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		FModuleTarget Target;
		if (!ResolveStage(Path, Emitter, Stage, Target, OutError, bOutNotFound))
		{
			return false;
		}
		UNiagaraNodeFunctionCall* Node = FindFunction(*Target.Output, Module, OutError);
		if (!Node)
		{
			bOutNotFound = true;
			return false;
		}
		const TArray<FInputRef> Inputs = ModuleInputs(*Target.Handle, *Node, Target.Usage);
		const FInputRef* Ref = Inputs.FindByPredicate([&Input](const FInputRef& R) { return ShortInputName(R.ModuleInput).Equals(Input, ESearchCase::IgnoreCase); });
		if (!Ref)
		{
			OutError = FString::Printf(TEXT("Module '%s' has no input '%s'."), *Module, *Input);
			bOutNotFound = true;
			return false;
		}
		UEdGraphPin* Override = OverridePin(*Node, Ref->AliasedName);
		if (!Override)
		{
			OutError = FString::Printf(TEXT("Input '%s' has no override to reset."), *Input);
			return false;
		}

		// Everything feeding the override pin that is not on the stage's main chain belongs to it
		// (the dynamic input, its own override node, nested dynamic inputs, parameter reads).
		const TSet<UEdGraphNode*> Chain = ChainNodes(*Target.Output);
		TArray<UEdGraphNode*> ToRemove;
		TArray<UEdGraphNode*> Pending;
		for (UEdGraphPin* Linked : Override->LinkedTo)
		{
			Pending.Add(Linked->GetOwningNode());
		}
		while (Pending.Num() > 0 && ToRemove.Num() < 500)
		{
			UEdGraphNode* Next = Pending.Pop();
			if (!Next || Chain.Contains(Next) || ToRemove.Contains(Next) || Next == Override->GetOwningNode())
			{
				continue;
			}
			ToRemove.Add(Next);
			for (UEdGraphPin* Pin : Next->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input)
				{
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						Pending.Add(Linked->GetOwningNode());
					}
				}
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("ResetInput", "UE CLI: Reset Niagara Module Input"));
		Target.System->Modify();
		UNiagaraGraph* Graph = Target.Output->GetNiagaraGraph();
		Graph->Modify();
		UEdGraphNode* OverrideOwner = Override->GetOwningNode();
		OverrideOwner->Modify();
		for (UEdGraphNode* Removed : ToRemove)
		{
			Removed->Modify();
			Removed->BreakAllNodeLinks();
			Graph->RemoveNode(Removed);
		}
		Override->BreakAllPinLinks();
		OverrideOwner->RemovePin(Override);
		Changed(*Target.System, Graph);
		Out = ModuleToJson(*Target.System, *Target.Handle, *Node, Target.Usage, 0);
		return true;
	}

	bool AddRenderer(const FString& Path, const FString& Emitter, const FString& Type, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter, OutError) : nullptr;
		bOutNotFound = Handle == nullptr;
		if (!Handle)
		{
			return false;
		}
		UClass* RendererClass = Type.Equals(TEXT("sprite"), ESearchCase::IgnoreCase) ? UNiagaraSpriteRendererProperties::StaticClass()
			: Type.Equals(TEXT("mesh"), ESearchCase::IgnoreCase) ? UNiagaraMeshRendererProperties::StaticClass()
			: Type.Equals(TEXT("ribbon"), ESearchCase::IgnoreCase) ? UNiagaraRibbonRendererProperties::StaticClass()
			: Type.Equals(TEXT("light"), ESearchCase::IgnoreCase) ? UNiagaraLightRendererProperties::StaticClass()
			: nullptr;
		if (!RendererClass)
		{
			OutError = FString::Printf(TEXT("Unknown renderer '%s' (sprite | mesh | ribbon | light)."), *Type);
			return false;
		}
		const FVersionedNiagaraEmitter Instance = Handle->GetInstance();
		UNiagaraEmitter* EmitterObject = Instance.Emitter;
		if (!EmitterObject || !Handle->GetEmitterData())
		{
			OutError = TEXT("The emitter has no editable data.");
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("AddRenderer", "UE CLI: Add Niagara Renderer"));
		System->Modify();
		EmitterObject->Modify();
		UNiagaraRendererProperties* Renderer = NewObject<UNiagaraRendererProperties>(EmitterObject, RendererClass, NAME_None, RF_Transactional);
		EmitterObject->AddRenderer(Renderer, Instance.Version);
		Changed(*System, nullptr);

		TArray<TSharedPtr<FJsonValue>> Renderers;
		for (const UNiagaraRendererProperties* Each : Handle->GetEmitterData()->GetRenderers())
		{
			if (Each)
			{
				Renderers.Add(MakeShared<FJsonValueString>(Each->GetClass()->GetName()));
			}
		}
		Out->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
		Out->SetArrayField(TEXT("renderers"), Renderers);
		return true;
	}

	bool SetEmitterSettings(const FString& Path, const FString& Emitter, const TSharedRef<FJsonObject>& Settings, TSharedRef<FJsonObject>& Out,
		FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter, OutError) : nullptr;
		bOutNotFound = Handle == nullptr;
		if (!Handle)
		{
			return false;
		}
		const FVersionedNiagaraEmitter Instance = Handle->GetInstance();
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		if (!Instance.Emitter || !Data)
		{
			OutError = TEXT("The emitter has no editable data.");
			return false;
		}
		FString SimTarget;
		const bool bHasSim = Settings->TryGetStringField(TEXT("simTarget"), SimTarget);
		if (bHasSim && SimTarget != TEXT("cpu") && SimTarget != TEXT("gpu"))
		{
			OutError = FString::Printf(TEXT("simTarget must be cpu or gpu (got '%s')."), *SimTarget);
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("EmitterSettings", "UE CLI: Niagara Emitter Settings"));
		System->Modify();
		Instance.Emitter->Modify();
		auto Notify = [&](FName Member)
		{
			if (FProperty* Property = FindFProperty<FProperty>(FVersionedNiagaraEmitterData::StaticStruct(), Member))
			{
				FPropertyChangedEvent Event(Property);
				Instance.Emitter->PostEditChangeVersionedProperty(Event, Instance.Version);
			}
		};
		if (bHasSim)
		{
			Data->SimTarget = SimTarget == TEXT("gpu") ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim;
			Notify(GET_MEMBER_NAME_CHECKED(FVersionedNiagaraEmitterData, SimTarget));
		}
		bool bFlag = false;
		if (Settings->TryGetBoolField(TEXT("localSpace"), bFlag))
		{
			Data->bLocalSpace = bFlag;
			Notify(GET_MEMBER_NAME_CHECKED(FVersionedNiagaraEmitterData, bLocalSpace));
		}
		if (Settings->TryGetBoolField(TEXT("determinism"), bFlag))
		{
			Data->bDeterminism = bFlag;
			Notify(GET_MEMBER_NAME_CHECKED(FVersionedNiagaraEmitterData, bDeterminism));
		}
		if (Settings->TryGetBoolField(TEXT("enabled"), bFlag))
		{
			Handle->SetIsEnabled(bFlag, *System, /*bRecompileIfChanged*/ false);
		}
		Changed(*System, nullptr);
		Out = EmitterToJson(*System, *Handle);
		return true;
	}

	bool RemoveRenderer(const FString& Path, const FString& Emitter, int32 Index, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		FNiagaraEmitterHandle* Handle = System ? FindHandle(*System, Emitter, OutError) : nullptr;
		bOutNotFound = Handle == nullptr;
		if (!Handle)
		{
			return false;
		}
		const FVersionedNiagaraEmitter Instance = Handle->GetInstance();
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		if (!Instance.Emitter || !Data)
		{
			OutError = TEXT("The emitter has no editable data.");
			return false;
		}
		const TArray<UNiagaraRendererProperties*> Renderers = Data->GetRenderers();
		if (!Renderers.IsValidIndex(Index) || !Renderers[Index])
		{
			OutError = FString::Printf(TEXT("Renderer index %d is out of range (the emitter has %d)."), Index, Renderers.Num());
			bOutNotFound = true;
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveRenderer", "UE CLI: Remove Niagara Renderer"));
		System->Modify();
		Instance.Emitter->Modify();
		Instance.Emitter->RemoveRenderer(Renderers[Index], Instance.Version);
		Changed(*System, nullptr);
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const UNiagaraRendererProperties* Each : Data->GetRenderers())
		{
			if (Each)
			{
				Names.Add(MakeShared<FJsonValueString>(Each->GetClass()->GetName()));
			}
		}
		Out->SetStringField(TEXT("emitter"), Handle->GetName().ToString());
		Out->SetArrayField(TEXT("renderers"), Names);
		return true;
	}

	bool Compile(const FString& Path, TSharedRef<FJsonObject>& Out, FString& OutError, bool& bOutNotFound)
	{
		UNiagaraSystem* System = LoadSystem(Path, OutError);
		bOutNotFound = System == nullptr;
		if (!System)
		{
			return false;
		}
		System->RequestCompile(/*bForce*/ false);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ false, /*bShowProgress*/ false);

		bool bSucceeded = true;
		TArray<TSharedPtr<FJsonValue>> ScriptsJson;
		auto Report = [&](const UNiagaraScript* Script, const FString& Emitter)
		{
			if (!Script)
			{
				return;
			}
			const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
			if (Status == ENiagaraScriptCompileStatus::NCS_Error || Status == ENiagaraScriptCompileStatus::NCS_Unknown)
			{
				bSucceeded = false;
			}
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			if (!Emitter.IsEmpty())
			{
				Json->SetStringField(TEXT("emitter"), Emitter);
			}
			FString Usage = StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage()));
			Json->SetStringField(TEXT("usage"), Usage);
			FString StatusName = StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue(static_cast<int64>(Status));
			StatusName.RemoveFromStart(TEXT("NCS_"));
			Json->SetStringField(TEXT("status"), StatusName);
			ScriptsJson.Add(MakeShared<FJsonValueObject>(Json));
		};
		Report(System->GetSystemSpawnScript(), FString());
		Report(System->GetSystemUpdateScript(), FString());
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData(); Data && Handle.GetIsEnabled())
			{
				TArray<UNiagaraScript*> Scripts;
				Data->GetScripts(Scripts, /*bCompilableOnly*/ true);
				for (const UNiagaraScript* Script : Scripts)
				{
					Report(Script, Handle.GetName().ToString());
				}
			}
		}
		Out->SetBoolField(TEXT("succeeded"), bSucceeded);
		Out->SetArrayField(TEXT("scripts"), ScriptsJson);
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
