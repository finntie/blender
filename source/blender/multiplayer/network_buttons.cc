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

#include "network_buttons.hh"
#include "network_connector.hh"

struct RestrictProperties {
  bool initialized = false;

  PropertyRNA *port_number;
  PropertyRNA *host_ip_number;
};

void draw_multiplayer_buttons(const bContext *C, Panel *panel)
{
  /* Get RNA properties (once for speed). Got from #outliner_draw.cc. */
  static RestrictProperties props = {false};
  if (!props.initialized) {
    props.port_number = RNA_struct_type_find_property(&RNA_Multiplayer, "port");
    props.host_ip_number = RNA_struct_type_find_property(&RNA_Multiplayer, "host_ip");
    props.initialized = true;
  }

  // Initialize class
  blender::multiplayer::initialize_network_class();

  Scene *scene = CTX_data_scene(C);

  uiBlock *block = panel->layout->block();

  /* Create buttons. */
  uiBut *bt;

  bt = uiDefBut(block, ButType::But, 0, IFACE_("Host"), 20, 130, 60, 20, nullptr, 0, 0, "");

  UI_but_func_set(bt, blender::multiplayer::host_same_device, (void *)"Random button!", nullptr);

  bt = uiDefBut(block, ButType::But, 0, IFACE_("Connect"), 20, 130, 60, 20, nullptr, 0, 0, "");

  UI_but_func_set(
      bt, blender::multiplayer::connect_same_device, (void *)"Random button!", nullptr);

  PointerRNA ptr = RNA_id_pointer_create(&scene->id);
  Multiplayer *mp = &scene->multiplayer;
  PointerRNA mu_ptr = RNA_pointer_create_discrete(&scene->id, &RNA_Multiplayer, mp);

  if (!mu_ptr.data) {
    printf("mu_ptr not initialize'\n");
  }

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
                          TIP_("Set port number of connection\n"
                               " \u2022 Second line"));
  
  UI_but_func_set(
      bt, blender::multiplayer::printRandomStatement, (void *)"Port number!", mp->host_ip);
}
