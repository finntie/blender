#include "network_connector.hh"
#include "network_base.hh"
#include <iostream>

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_library.hh"
#include "BKE_main.hh"
#include "BKE_screen.hh"

#include "DEG_depsgraph.hh"

#include "DNA_object_types.h"

#include "WM_api.hh"
#include "WM_types.hh"

#include "BLI_array.hh"
#include "BLI_listbase.h"

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

void printRandomStatement(bContext *, void *, void *poin2)
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

void initialize_network_class(const bContext *C)
{
  CurrentContext = C;

  MU_class_object.MU_init(true, false);

  // Create packages
  MU_class_object.MU_create_package(
      "Object_Transform", "name", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  MU_class_object.MU_create_package_callback_function("Object_Transform", MU_package_transform);
}

void host_same_device(bContext *, void *, void *)
{
  MU_class_object.MU_host(Dance::SAMEDEVICE, 10);
  reset_scene();
}

void connect_same_device(bContext *, void *, void *)
{
  if (MU_class_object.MU_connect(Dance::SAMEDEVICE, "192.168.0.2")) {
    // Succeeded
    reset_scene();
  }
}

void MU_handle_transform(Object *ob)
{
  if (ob) {
    Main *bmain = CTX_data_main(CurrentContext);

    if (bmain) {
      BKE_libblock_rename(*bmain, ob->id, "CubeName");
    }
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

void reset_scene()
{
  Main *bmain = CTX_data_main(CurrentContext);
  Scene *scene = CTX_data_scene(CurrentContext);
  ViewLayer *view_layer = CTX_data_view_layer(CurrentContext);
  BKE_view_layer_synced_ensure(scene, view_layer);

  blender::Vector<Object *> delete_objects;
  LISTBASE_FOREACH (CollectionObject *, cob, &view_layer->active_collection->collection->gobject) {
    delete_objects.append(cob->ob);
  }

  for (Object *ob : delete_objects) {
    BKE_id_delete(bmain, ob);
  }

  WM_event_add_notifier(CurrentContext, NC_SCENE | ND_OB_ACTIVE, scene);
}

void MU_package_transform(const std::string &buffer)
{
  // printf("Received message callback\n");
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

}  // namespace blender::multiplayer
