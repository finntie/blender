#include "BLT_translation.hh"

#include "DNA_scene_types.h"

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_layer.hh"
#include "BKE_library.hh"
#include "BKE_screen.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "BLI_string_utf8.h"
#include "BLI_listbase.h"

#include "network_buttons.hh"
#include "network_connector.hh"

namespace blender::multiplayer {

struct RestrictProperties {
  bool initialized = false;

  PropertyRNA *port_number;
  PropertyRNA *host_ip_number;
  PropertyRNA *connection_type;
  PropertyRNA *use_ipv4;
};

void MU_panel_register(ARegionType *art)
{
  PanelType *pt;

  /* Multiplayer UI */
  pt = MEM_callocN<PanelType>("spacetype view3d panel multiplayer");
  STRNCPY_UTF8(pt->idname, "VIEW3D_PT_multiplayer");
  STRNCPY_UTF8(pt->label, N_("Multiplayer Settings"));
  STRNCPY_UTF8(pt->category, "Multiplayer");
  STRNCPY_UTF8(pt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  pt->draw = blender::multiplayer::MU_draw_multiplayer_buttons;
  BLI_addtail(&art->paneltypes, pt);
}

void MU_draw_multiplayer_buttons(const bContext *C, Panel *panel)
{
  /* Get RNA properties (once for speed). Got from #outliner_draw.cc. */
  static RestrictProperties props = {false};
  if (!props.initialized) {
    props.port_number = RNA_struct_type_find_property(&RNA_Multiplayer, "port");
    props.host_ip_number = RNA_struct_type_find_property(&RNA_Multiplayer, "host_ip");
    props.connection_type = RNA_struct_type_find_property(&RNA_Multiplayer, "connection_type");
    props.use_ipv4 = RNA_struct_type_find_property(&RNA_Multiplayer, "use_ipv4");
    props.initialized = true;
  }

  Scene *scene = CTX_data_scene(C);

  uiBlock *block = panel->layout->block();

  /* Create buttons. */
  uiBut *bt;

  Multiplayer *mp = &scene->multiplayer;
  PointerRNA mu_ptr = RNA_pointer_create_discrete(&scene->id, &RNA_Multiplayer, mp);

  if (!mu_ptr.data) {
    printf("mu_ptr not initialize'\n");
  }

  /* Port Number */

  bt = uiDefButR_prop(block,
                      ButType::Num,
                      1,
                      IFACE_(""),
                      100,
                      200,
                      UI_UNIT_X,
                      UI_UNIT_Y,
                      &mu_ptr,
                      props.port_number,
                      -1,
                      0,
                      0,
                      TIP_("Set port number of connection\n"
                           " \u2022 Second line"));

  UI_but_func_set(bt, blender::multiplayer::MU_printRandomStatement, mp, nullptr);

  /* Network Type */

  bt = uiDefButR_prop(block,
                      ButType::Menu,
                      0,
                      IFACE_("Network Type"),
                      100,
                      200,
                      UI_UNIT_X,
                      UI_UNIT_Y,
                      &mu_ptr,
                      props.connection_type,
                      -1,
                      0,
                      0,
                      TIP_("Set the connection type\n"
                           " \u2022 Second line"));

  UI_but_func_set(bt, blender::multiplayer::MU_printRandomStatement, mp, nullptr);

  /* Host/Peer IP */

  bt = uiDefButR_prop(block,
                      ButType::Text,
                      0,
                      IFACE_(""),
                      100,
                      200,
                      UI_UNIT_X,
                      UI_UNIT_Y,
                      &mu_ptr,
                      props.host_ip_number,
                      -1,
                      0,
                      0,
                      TIP_("Insert IP of host/client\n"
                           " \u2022 Second line"));

  //UI_but_func_set(bt, blender::multiplayer::MU_printRandomStatement, mp, nullptr);
  bt = uiDefBut(block,
                ButType::But,
                0,
                IFACE_("Add"),
                100,
                100,
                UI_UNIT_X * 3,
                UI_UNIT_Y,
                nullptr,
                0,
                0,
                TIP_ ("Set/Add IP"));

  UI_but_func_set(bt, blender::multiplayer::MU_add_client_to_vector, mp, nullptr);

  /* Force #IPV4 */

  bt = uiDefButR_prop(block,
                      ButType::ButToggle,
                      0,
                      IFACE_("Force IPV4"),
                      100,
                      200,
                      UI_UNIT_X * 0.25f,
                      UI_UNIT_Y,
                      &mu_ptr,
                      props.use_ipv4,
                      -1,
                      0,
                      0,
                      TIP_("Force IPV4, else if the network supports it, it could turn into ip6. \n"
                           " \u2022 Second line"));

  UI_but_func_set(bt, blender::multiplayer::MU_printRandomStatement, mp, nullptr);

  /* Connect/Host buttons */

  bt = uiDefBut(block, ButType::But, 0, IFACE_("Host"), 20, 130, 16, 20, nullptr, 0, 0, "");

  UI_but_func_set(bt, blender::multiplayer::MU_host_same_device, mp, nullptr);

  bt = uiDefBut(block, ButType::But, 0, IFACE_("Connect"), 20, 130, 16, 20, nullptr, 0, 0, "");

  UI_but_func_set(bt, blender::multiplayer::MU_connect_same_device, mp, nullptr);

  blender::Vector<std::string> client_ip_vector = multiplayer::MU_get_clients_vector();
  for (int i = 0; i < client_ip_vector.size(); i++) {
    bt = uiDefBut(
        block, ButType::Label, 0, IFACE_(client_ip_vector[i].c_str()), 20, 130, 16, 20, nullptr, 0, 0, "");
  }

}

}  // namespace blender::multiplayer
