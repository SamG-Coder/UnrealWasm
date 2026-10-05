import unreal

assets=unreal.EditorAssetLibrary
if not assets.does_asset_exist('/Game/FixtureCube'):
    mesh=assets.duplicate_asset('/Engine/BasicShapes/Cube','/Game/FixtureCube')
else:
    mesh=assets.load_asset('/Game/FixtureCube')
meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
settings=meshes.get_nanite_settings(mesh)
settings.set_editor_property('enabled',True)
meshes.set_nanite_settings(mesh,settings,True)
assets.save_loaded_asset(mesh)
if not assets.does_asset_exist('/Game/FixtureMaterial'):
    mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('FixtureMaterial','/Game',unreal.Material,unreal.MaterialFactoryNew())
    edit=unreal.MaterialEditingLibrary
    color=edit.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-300,0)
    color.set_editor_property('parameter_name','Color')
    color.set_editor_property('default_value',unreal.LinearColor(.5,.5,.5,1))
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for name,prop,y,value in [('Roughness',unreal.MaterialProperty.MP_ROUGHNESS,180,.4),('Metallic',unreal.MaterialProperty.MP_METALLIC,300,0)]:
        node=edit.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-300,y)
        node.set_editor_property('parameter_name',name)
        node.set_editor_property('default_value',value)
        edit.connect_material_property(node,'',prop)
    edit.recompile_material(mat)
    assets.save_loaded_asset(mat)
mat=assets.load_asset('/Game/FixtureMaterial')
mat.set_editor_property('used_with_nanite',True)
unreal.MaterialEditingLibrary.recompile_material(mat)
assets.save_loaded_asset(mat)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if assets.does_asset_exist('/Game/Sample'):
    assert levels.load_level('/Game/Sample'), 'Could not load fixture map'
else:
    assert levels.new_level('/Game/Sample'), 'Could not create fixture map'
assert levels.save_current_level(), 'Could not save fixture map'
unreal.log('SAMPLE_ASSETS_READY: Nanite cube, material, empty runtime fixture map')
unreal.SystemLibrary.quit_editor()
