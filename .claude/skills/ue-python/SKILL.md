---
name: ue-python
description: "Tested Unreal editor Python snippets for ue_run_python: Animation Blueprints, material graphs, Enhanced Input, Data Tables, new levels, Level Sequences, sound, foliage, nav mesh, streaming levels."
---

# Python recipes (`ue_run_python` / `uecli python`)

Snippets for editor areas with no dedicated tool. Each one was run against UE 5.7
(C:\ueh57, `-nullrhi` or `-RenderOffscreen` for anything that spawns actors) and produced the printed result. Paths under `/Game/T4x/`
are examples. Save the assets afterwards with `ue_save_asset`.

## Animation Blueprint

```python
import unreal
f = unreal.AnimBlueprintFactory()
f.set_editor_property("parent_class", unreal.AnimInstance)
f.set_editor_property("target_skeleton", unreal.load_asset("/Engine/EngineMeshes/SkeletalCube_Skeleton"))
abp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("ABP_Hero", "/Game/Anim", unreal.AnimBlueprint, f)
```

Then build the anim graph with `ue_add_node` type `Node` (`AnimGraphNode_StateMachine`,
`AnimStateNode`, …) — see `ue_add_node`.

## Material graph

```python
import unreal
L = unreal.MaterialEditingLibrary
m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Tint", "/Game/Mat", unreal.Material, unreal.MaterialFactoryNew())
tint = L.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, 0)
tint.set_editor_property("parameter_name", "Tint")
rough = L.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
mul = L.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 0)
L.connect_material_expressions(tint, "RGB", mul, "A")
L.connect_material_expressions(rough, "", mul, "B")
L.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
L.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
L.recompile_material(m)
```

Check the result with `ue_get_material_graph`.

## Enhanced Input (action + mapping context)

There is no `InputActionFactory` in Python. Pass `None` as the factory.

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
ia = tools.create_asset("IA_Jump", "/Game/Input", unreal.InputAction, None)
ia.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
imc = tools.create_asset("IMC_Default", "/Game/Input", unreal.InputMappingContext, None)
key = unreal.Key()
key.import_text("SpaceBar")
imc.map_key(ia, key)
```

## Data Table from CSV

```python
import unreal
f = unreal.DataTableFactory()
f.set_editor_property("struct", unreal.load_object(None, "/Script/GameplayTags.GameplayTagTableRow"))
dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_Items", "/Game/Data", unreal.DataTable, f)
unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, "Name,Tag,DevComment\nA,Item.A,first\n")
print(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
```

Use your own row struct (`/Script/<Module>.<Struct>` or a User Defined Struct asset) the same way.

## New level

```python
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Maps/L_Arena")
```

This switches the open level. Save or discard the current one first.

## Blend Space, Anim Sequence, Montage

```python
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
skel = unreal.load_asset("/Engine/EngineMeshes/SkeletalCube_Skeleton")
f = unreal.BlendSpaceFactoryNew()
f.set_editor_property("target_skeleton", skel)
tools.create_asset("BS_Move", "/Game/Anim", unreal.BlendSpace, f)
sf = unreal.AnimSequenceFactory()
sf.set_editor_property("target_skeleton", skel)
seq = tools.create_asset("AS_Idle", "/Game/Anim", unreal.AnimSequence, sf)
mf = unreal.AnimMontageFactory()
mf.set_editor_property("target_skeleton", skel)
mf.set_editor_property("source_animation", seq)
tools.create_asset("AM_Idle", "/Game/Anim", unreal.AnimMontage, mf)
```

## Sound Cue

```python
import unreal
unreal.AssetToolsHelpers.get_asset_tools().create_asset("SC_Click", "/Game/Audio", unreal.SoundCue, unreal.SoundCueFactoryNew())
```

## Level Sequence with an actor binding and a transform track

```python
import unreal
seq = unreal.AssetToolsHelpers.get_asset_tools().create_asset("LS_Intro", "/Game/Cine", unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
seq.set_playback_start(0)
seq.set_playback_end(120)
actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
binding = seq.add_possessable(actor)
track = binding.add_track(unreal.MovieScene3DTransformTrack)
track.add_section().set_range(0, 120)
```

## Niagara system placed in the level

```python
import unreal
ns = unreal.AssetToolsHelpers.get_asset_tools().create_asset("NS_Spark", "/Game/FX", unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.NiagaraActor, unreal.Vector(0, 0, 100))
actor.get_editor_property("niagara_component").set_asset(ns)
```

`uecli niagara create` (MCP `ue_create_niagara_system`) creates the system without Python.
Fill it with `uecli niagara add-emitter` / `add-module` / `set-input`
(MCP: `ue_add_niagara_emitter`, ...) — Python has no API for emitters and modules.

## Landscape material with paint layers

```python
import unreal
m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Terrain", "/Game/Landscape", unreal.Material, unreal.MaterialFactoryNew())
mel = unreal.MaterialEditingLibrary
blend = mel.create_material_expression(m, unreal.MaterialExpressionLandscapeLayerBlend, -400, 0)
layers = []
for name in ("Grass", "Rock"):
    info = unreal.LayerBlendInput()
    info.set_editor_property("layer_name", name)
    info.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
    layers.append(info)
blend.set_editor_property("layers", layers)
mel.connect_material_property(blend, "", unreal.MaterialProperty.MP_BASE_COLOR)
mel.recompile_material(m)
```

Then `uecli landscape create --material /Game/Landscape/M_Terrain`; `uecli landscape layers`
reports `materialLayers: Grass, Rock` — add those with `landscape add-layer` and paint.
Feed each layer's inputs (textures / colors) into the blend node with `connect_material_expressions`.

## Foliage scattered on the landscape

```python
import random, unreal
ft = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    "FT_Rock", "/Game/Foliage", unreal.FoliageType_InstancedStaticMesh, unreal.FoliageType_InstancedStaticMeshFactory())
ft.set_editor_property("mesh", unreal.load_asset("/Engine/BasicShapes/Cube"))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
transforms = []
for _ in range(50):
    x, y = random.uniform(-2000, 2000), random.uniform(-2000, 2000)
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 100000), unreal.Vector(x, y, -100000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit:
        transforms.append(unreal.Transform(hit.to_tuple()[4], unreal.Rotator(0, 0, random.uniform(0, 360)), unreal.Vector(0.3, 0.3, 0.3)))
unreal.InstancedFoliageActor.add_instances(world, ft, transforms)
```

Printed `TRACED 50 INSTANCES 50` on the World Partition template map (counted from the
`InstancedStaticMeshComponent`s of every `InstancedFoliageActor`).
`unreal.InstancedFoliageActor.remove_all_instances(world, ft)` clears them again.

## Nav mesh bounds + build

```python
import unreal
v = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 0))
v.set_actor_scale3d(unreal.Vector(20, 20, 5))
```

Then build navigation with `uecli build-level paths` (`level.build` job).

## Anim notify on a sequence

```python
import unreal
sf = unreal.AnimSequenceFactory()
sf.set_editor_property("target_skeleton", unreal.load_asset("/Engine/EngineMeshes/SkeletalCube_Skeleton"))
seq = unreal.AssetToolsHelpers.get_asset_tools().create_asset("AS_Step", "/Game/Anim", unreal.AnimSequence, sf)
L = unreal.AnimationLibrary
L.add_animation_notify_track(seq, "Events")
L.add_animation_notify_event(seq, "Events", 0.0, unreal.AnimNotify_PlaySound)
print(len(L.get_animation_notify_events(seq)))   # 1
```

## Sound Class and MetaSound Source

```python
import unreal
t = unreal.AssetToolsHelpers.get_asset_tools()
sc = t.create_asset("SC_Music", "/Game/Audio", unreal.SoundClass, unreal.SoundClassFactory())
sc.get_editor_property("properties").set_editor_property("volume", 0.5)
ms = t.create_asset("MS_Beep", "/Game/Audio", unreal.MetaSoundSource, unreal.MetaSoundSourceFactory())
```

### MetaSound graph (builder API)

```python
import unreal
bs = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
builder, on_play, on_finished, audio_outs, res = bs.create_source_builder("MSB_Tone", unreal.MetaSoundOutputAudioFormat.MONO, True)
sine, res = builder.add_node_by_class_name(unreal.MetasoundFrontendClassName(namespace="UE", name="Sine", variant="Audio"))
out, res = builder.find_node_output_by_name(sine, "Audio")
builder.connect_nodes(out, audio_outs[0])            # MetaSoundBuilderResult.SUCCEEDED
asset, res = unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem).build_to_asset(builder, "me", "MS_Tone", "/Game/Audio")
```

Every builder call returns a `MetaSoundBuilderResult`; check it, a wrong node or pin name fails quietly otherwise.

## Streaming sub-level

```python
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.new_level("/Game/Maps/Sub_A")
les.save_current_level()
les.new_level("/Game/Maps/Main")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.EditorLevelUtils.add_level_to_world(world, "/Game/Maps/Sub_A", unreal.LevelStreamingDynamic)
les.save_current_level()
print([l.get_path_name() for l in unreal.EditorLevelUtils.get_levels(world)])
# ['/Game/Maps/Main.Main:PersistentLevel', '/Game/Maps/Sub_A.Sub_A:PersistentLevel']
```

`new_level` switches the open level: save the current one first.

## Spawning actors needs a rendering editor

This needs an editor that renders. Under `-nullrhi`, spawning any actor (Python or
`ue_spawn_actor`) crashes UE 5.7 with `EXCEPTION_INT_DIVIDE_BY_ZERO` inside
`SpawnActorFromClass`. `-RenderOffscreen` works.
