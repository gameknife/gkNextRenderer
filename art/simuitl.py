# blender script

# set rigidbody to passive for all selected objects
import bpy

for obj in bpy.context.selected_objects:
    obj.rigid_body.kinematic = True
    obj.rigid_body.type = 'PASSIVE'
    obj.rigid_body.kinematic = False
    