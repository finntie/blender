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
#include "BLI_string_utf8.h"

#include "ED_mesh.hh"
#include "ED_object.hh"
#include "ED_screen.hh"

#include "DNA_scene_types.h"


namespace blender::multiplayer {

Dance MU_class_object;
const bContext *CurrentContext;
static std::string ownPrivateIP;
static std::string ownPublicIP;

static blender::Vector<std::string> ClientIPs;

void MU_initialize_network_class(const bContext *C)
{
  CurrentContext = C;

  MU_class_object.MU_init(true, false);

  ownPrivateIP = MU_class_object.MU_get_IP(false);
  ownPublicIP = MU_class_object.MU_get_IP(true);

  /* Register and call operator */
  WM_operatortype_append(MU_timer_operator);

  /* Create packages */
  /* Contains: loc.x, loc.y, loc.z, rot.x, rot.y, rot.z, scale.x, scale.y, scale.z */
  MU_class_object.MU_create_package(
      "Object_Transform", "name", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  MU_class_object.MU_create_package_callback_function("Object_Transform", MU_package_transform);

  /* Create callback for custom message */
  MU_class_object.MU_create_package_callback_function("Change_Object", MU_package_update_object);
}

void MU_add_client_to_vector(bContext *, void *poin, void *)
{
  Multiplayer *mp = static_cast<Multiplayer *>(poin);
  /* At least 4 of size. */
  if (strlen(mp->host_ip) > 4) {
  ClientIPs.append(mp->host_ip);
  }
}

void MU_remove_client_from_vector(bContext *, void *poin, void *)
{
  int* index = static_cast<int*>(poin);
  printf("index: %d\n", *index);
  if (*index >= 0 && *index < ClientIPs.size()) {
    ClientIPs.remove(*index);
  }
}

std::string MU_get_own_private_IP()
{
  return ownPrivateIP;
}

std::string MU_get_own_public_IP()
{
  return ownPublicIP;
}

void MU_force_IPV4_button(bContext *, void *poin, void *)
{
  /* Refresh IP */
  Multiplayer *mp = static_cast<Multiplayer *>(poin);
  if (mp) {
    bool forceIPV4 = mp->use_ipv4;
    MU_class_object.MU_set_force_IPV4(forceIPV4);
    ownPublicIP = MU_class_object.MU_get_IP(true);
  }
}

static void MU_text_to_clipboard(std::string& text)
{
  // Copy Code to clipboard:
  if (OpenClipboard(GW_HWNDFIRST)) {
    // Using windows global memory.
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.length() + 1);
    if (hMem) {
      auto hMemLock = GlobalLock(hMem);
      if (hMemLock) {
        memcpy(hMemLock, text.c_str(), text.length());
        ((char *)hMemLock)[text.length()] = '\0';
        GlobalUnlock(hMem);

        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
      }
      else {
        GlobalFree(hMem);
      }
    }
    CloseClipboard();
  }
}

void MU_private_IP_to_clipboard(bContext *, void *, void *)
{
  MU_text_to_clipboard(ownPrivateIP);
}

void MU_public_IP_to_clipboard(bContext *, void *, void *)
{
  MU_text_to_clipboard(ownPublicIP); 
}

blender::Vector<std::string> MU_get_clients_vector()
{
  return ClientIPs;
}

void MU_host(bContext *, void *poin, void *)
{
  Dance::dance_moves moves = Dance::SAMEDEVICE;
  int port = 8392;
  bool force_ipv4 = false;

  Multiplayer *mp = static_cast<Multiplayer *>(poin);
  if (mp) {
    moves = static_cast<Dance::dance_moves>(mp->connection_type);
    port = mp->port;
    force_ipv4 = static_cast<bool>(mp->use_ipv4);
  }

  WM_operator_name_call(const_cast<bContext *>(CurrentContext),
                        "NETWORK_MU_connection_alive",
                        blender::wm::OpCallContext::InvokeDefault,
                        nullptr,
                        nullptr);

  MU_class_object.MU_host(moves, 10, std::to_string(port).c_str(), force_ipv4, ClientIPs);
  // blender::multiplayer::MU_reset_scene(CurrentContext);
}

void MU_connect(bContext *, void *poin, void *)
{
  Dance::dance_moves moves = Dance::SAMEDEVICE;
  int port = 8392;
  bool force_ipv4 = false;

  Multiplayer *mp = static_cast<Multiplayer *>(poin);
  if (mp) {
    moves = static_cast<Dance::dance_moves>(mp->connection_type);
    port = mp->port;
    force_ipv4 = static_cast<bool>(mp->use_ipv4);
  }

  if (MU_class_object.MU_connect(
          moves, ClientIPs[0].c_str(), std::to_string(port).c_str(), force_ipv4))
  {

    WM_operator_name_call(const_cast<bContext *>(CurrentContext),
                          "NETWORK_MU_connection_alive",
                          blender::wm::OpCallContext::InvokeDefault,
                          nullptr,
                          nullptr);
    // Succeeded
    // blender::multiplayer::MU_reset_scene(CurrentContext);
  }
}

void MU_handle_transform(Object *ob)
{
  if (ob && MU_class_object.MU_get_total_connections() > 0) {

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
  if (ob && MU_class_object.MU_get_total_connections() > 0) {

    std::string message = MU_object_to_message(ob, "Change_Object");

    MU_class_object.MU_send_message_to(message, 0, false, true, true);
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
      float loc[3] = {MU_class_object.MU_data_to_variable<float>(buffer, 3),
                      MU_class_object.MU_data_to_variable<float>(buffer, 4),
                      MU_class_object.MU_data_to_variable<float>(buffer, 5)};

      float rot[3] = {MU_class_object.MU_data_to_variable<float>(buffer, 6),
                      MU_class_object.MU_data_to_variable<float>(buffer, 7),
                      MU_class_object.MU_data_to_variable<float>(buffer, 8)};

      float scale[3] = {MU_class_object.MU_data_to_variable<float>(buffer, 9),
                        MU_class_object.MU_data_to_variable<float>(buffer, 10),
                        MU_class_object.MU_data_to_variable<float>(buffer, 11)};

      if (memcmp(loc, ob->loc, 3 * sizeof(float)) && memcmp(rot, ob->rot, 3 * sizeof(float)) &&
          memcmp(scale, ob->scale, 3 * sizeof(float)))
      {
        return;
      }
      else if (loc[0] == ob->loc[0] && loc[1] == ob->loc[1] && loc[2] == ob->loc[2] &&
               rot[0] == ob->rot[0] && rot[1] == ob->rot[1] && rot[2] == ob->rot[2] &&
               scale[0] == ob->scale[0] && scale[1] == ob->scale[1] && scale[2] == ob->scale[2])
      {
        return;
      }

      memcpy(ob->loc, loc, 3 * sizeof(float));
      memcpy(ob->rot, rot, 3 * sizeof(float));
      memcpy(ob->scale, scale, 3 * sizeof(float));

      /* Notify blender that this object changed its transform. */
      DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM);
      WM_event_add_notifier(CurrentContext, NC_OBJECT | ND_TRANSFORM | NS_NETWORK, CTX_data_scene(CurrentContext));
    }
  }
}

void MU_package_update_object(const std::string &buffer)
{
  /* Create object using message */
  MU_message_to_object(const_cast<bContext *>(CurrentContext), buffer);
}

/* -------------------------------------------------------------------- */
/** \Operator types
 * \{ */

static wmOperatorStatus MU_operator_invoke_timer(bContext *C, wmOperator *op, const wmEvent *event)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  wmWindow *window = CTX_wm_window(C);

  wmTimer *timer = WM_event_timer_add(wm, window, wmEventType::TIMER, 0.1f);
  op->customdata = timer;

  WM_event_add_modal_handler(C, op);
  return OPERATOR_RUNNING_MODAL;
}

static wmOperatorStatus MU_operator_modal_timer(bContext *C, wmOperator *op, const wmEvent *event)
{
  wmTimer *timer = reinterpret_cast<wmTimer *>(op->customdata);

  if (event->type == wmEventType::TIMER && event->customdata == timer) {
    /* Keep connection alive to enable for important message checks */
    MU_class_object.MU_keep_alive(timer->time_duration);
  }

  return OPERATOR_PASS_THROUGH;
}

void MU_timer_operator(wmOperatorType *ot)
{
  /*Create timer that is called every 0.1 seconds. */
  ot->name = "Keep Connection Alive";
  ot->idname = "NETWORK_MU_connection_alive";

  ot->invoke = MU_operator_invoke_timer;
  ot->modal = MU_operator_modal_timer;

  /* flags */
  ot->flag = 0;
}
/** \} */

}  // namespace blender::multiplayer
