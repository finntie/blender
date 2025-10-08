#include "network_connector.hh"
#include "network_base.hh"
#include "network_scene.hh"
#include <iostream>

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_library.hh"
#include "BKE_main.hh"
#include "BKE_mesh.hh"
#include "BKE_screen.hh"

#include "DEG_depsgraph.hh"

#include "DNA_object_types.h"

#include "WM_api.hh"
#include "WM_types.hh"

#include "BLI_array.hh"
#include "BLI_listbase.h"

#include "ED_mesh.hh"
#include "ED_object.hh"

/* TODO: what is needed? */
#include "DNA_armature_types.h"
#include "DNA_collection_types.h"
#include "DNA_constraint_types.h"
#include "DNA_gpencil_legacy_types.h"
#include "DNA_light_types.h"
#include "DNA_lightprobe_types.h"
#include "DNA_object_force_types.h"
#include "DNA_scene_types.h"
#include "DNA_sequence_types.h"
#include "DNA_text_types.h"

#include "BKE_armature.hh"
#include "BKE_curve.hh"
#include "BKE_deform.hh"
#include "BKE_gpencil_legacy.h"
#include "BKE_grease_pencil.hh"
#include "BKE_idtype.hh"
#include "BKE_lib_override.hh"
#include "BKE_main_namemap.hh"
#include "BKE_modifier.hh"
#include "BKE_node.hh"
#include "BKE_object.hh"
#include "BKE_particle.h"
#include "BKE_report.hh"
#include "BKE_scene.hh"

namespace blender::multiplayer {

Dance MU_class_object;
const bContext *CurrentContext;

void MU_printRandomStatement(bContext *, void *, void *poin2)
{
  const char *message = static_cast<const char *>(poin2);
  printf("Message containing: %s, will be send:\n", message);

  if (MU_class_object.MU_get_if_host()) {
    MU_class_object.MU_send_message_to(std::string(message), 1, false, false);
  }
  else {
    MU_class_object.MU_send_message_to(std::string(message), 0, false, false);
  }
}

void MU_initialize_network_class(const bContext *C)
{
  CurrentContext = C;

  MU_class_object.MU_init(true, false);

  /* Create packages */
  /* Contains: loc.x, loc.y, loc.z, rot.x, rot.y, rot.z, scale.x, scale.y, scale.z */
  MU_class_object.MU_create_package(
      "Object_Transform", "name", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  MU_class_object.MU_create_package_callback_function("Object_Transform", MU_package_transform);

  /* Create callback for custom message */
  MU_class_object.MU_create_package_callback_function("Change_Object", MU_package_update_object);
}

void MU_host_same_device(bContext *, void *, void *)
{
  MU_class_object.MU_host(Dance::SAMEDEVICE, 10);
  // blender::multiplayer::MU_reset_scene(CurrentContext);
}

void MU_connect_same_device(bContext *, void *, void *)
{
  if (MU_class_object.MU_connect(Dance::SAMEDEVICE, "192.168.0.0")) {
    // Succeeded
    // blender::multiplayer::MU_reset_scene(CurrentContext);
  }
}

void MU_handle_transform(Object *ob)
{
  if (ob) {

    //Main *bmain = CTX_data_main(CurrentContext);

    //if (bmain) {
    //  BKE_libblock_rename(*bmain, ob->id, "CubeName");
    //}
    std::string objectName = ob->id.name;

    MU_class_object.MU_add_data_to_parameter<std::string>("Object_Transform", 0, objectName);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 1, ob->loc[0]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 2, ob->loc[1]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 3, ob->loc[2]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 4, ob->rot[0]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 5, ob->rot[1]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 6, ob->rot[2]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 7, ob->scale[0]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 8, ob->scale[1]);
    MU_class_object.MU_add_data_to_parameter<float>("Object_Transform", 9, ob->scale[2]);

    MU_class_object.MU_send_package("Object_Transform");
  }
  else {
    printf("Something went wrong casting the object\n");
  }
}

void MU_layer_update(Object *ob)
{
  if (ob) {

    std::string message = MU_object_to_message(ob, "Change_Object");

    /* TODO: do not hardcode towards who. */
    if (MU_class_object.MU_get_if_host()) {
      MU_class_object.MU_send_message_to(message, 1, false, true);
      printf("Send Message: %s\n", message.c_str());
    }
    else {
      MU_class_object.MU_send_message_to(message, 0, true, true);
    }

    //std::string objectName = ob->id.name;
    //MU_class_object.MU_add_data_to_parameter<std::string>("Change_Object", 0, objectName);
    //MU_class_object.MU_add_data_to_parameter<int>("Change_Object", 1, ob->type);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 2, ob->loc[0]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 3, ob->loc[1]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 4, ob->loc[2]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 5, ob->rot[0]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 6, ob->rot[1]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 7, ob->rot[2]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 8, ob->scale[0]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 9, ob->scale[1]);
    //MU_class_object.MU_add_data_to_parameter<float>("Change_Object", 10, ob->scale[2]);
    //MU_class_object.MU_add_data_to_parameter<int>("Change_Object", 11, MU_get_default_shape(ob));

    //MU_class_object.MU_send_package("Change_Object");
  }
}

void MU_package_transform(const std::string &buffer)
{
  /* 0 and 1 are name and client. */
  std::string objectID = std::string(MU_class_object.get_word(buffer, 2));

  Main *bmain = CTX_data_main(CurrentContext);
  if (bmain) {
    Object *ob = reinterpret_cast<Object *>(
        BKE_libblock_find_name(bmain, ID_OB, objectID.c_str() + 2));

    if (ob) {
      ob->loc[0] = MU_class_object.MU_data_to_variable<float>(buffer, 3);
      ob->loc[1] = MU_class_object.MU_data_to_variable<float>(buffer, 4);
      ob->loc[2] = MU_class_object.MU_data_to_variable<float>(buffer, 5);

      ob->rot[0] = MU_class_object.MU_data_to_variable<float>(buffer, 6);
      ob->rot[1] = MU_class_object.MU_data_to_variable<float>(buffer, 7);
      ob->rot[2] = MU_class_object.MU_data_to_variable<float>(buffer, 8);

      ob->scale[0] = MU_class_object.MU_data_to_variable<float>(buffer, 9);
      ob->scale[1] = MU_class_object.MU_data_to_variable<float>(buffer, 10);
      ob->scale[2] = MU_class_object.MU_data_to_variable<float>(buffer, 11);

      /* Notify blender that this object changed its transform. */
      DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
      WM_event_add_notifier(CurrentContext, NC_OBJECT | ND_TRANSFORM, ob);
    }
  }
}

void MU_package_update_object(const std::string &buffer)
{
  /* Create object using message */
  MU_message_to_object(const_cast<bContext *>(CurrentContext), buffer);

  ///* 0 and 1 are name and client. */
  //std::string objectID = std::string(MU_class_object.get_word(buffer, 2));

  //Main *bmain = CTX_data_main(CurrentContext);
  //if (bmain) {

  //  float loc[3];
  //  float rot[3];
  //  float scale[3];

  //  loc[0] = MU_class_object.MU_data_to_variable<float>(buffer, 4);
  //  loc[1] = MU_class_object.MU_data_to_variable<float>(buffer, 5);
  //  loc[2] = MU_class_object.MU_data_to_variable<float>(buffer, 6);
  //  rot[0] = MU_class_object.MU_data_to_variable<float>(buffer, 7);
  //  rot[1] = MU_class_object.MU_data_to_variable<float>(buffer, 8);
  //  rot[2] = MU_class_object.MU_data_to_variable<float>(buffer, 9);
  //  scale[0] = MU_class_object.MU_data_to_variable<float>(buffer, 10);
  //  scale[1] = MU_class_object.MU_data_to_variable<float>(buffer, 11);
  //  scale[2] = MU_class_object.MU_data_to_variable<float>(buffer, 12);

  //  /* Check if object already exists, if so, update it.*/
  //  Object *ob = reinterpret_cast<Object *>(
  //      BKE_libblock_find_name(bmain, ID_OB, objectID.c_str() + 2));
  //  if (ob) {
  //    ob->loc[0] = loc[0];
  //    ob->loc[1] = loc[1];
  //    ob->loc[2] = loc[2];
  //    ob->rot[0] = rot[0];
  //    ob->rot[1] = rot[1];
  //    ob->rot[2] = rot[2];
  //    ob->scale[0] = scale[0];
  //    ob->scale[1] = scale[1];
  //    ob->scale[2] = scale[2];
  //  }
  //  else {
  //    /* Create object */
  //    int type = MU_class_object.MU_data_to_variable<int>(buffer, 3);
  //    blender::ed::object::add_type(
  //        const_cast<bContext *>(CurrentContext), type, objectID.c_str(), loc, rot, false, 0);
  //  }
  //}
}

}  // namespace blender::multiplayer
