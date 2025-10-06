#include "network_buttons.hh"


#include "BLT_translation.hh"

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_library.hh"
#include "BKE_screen.hh"

#include "UI_interface.hh"
#include "UI_interface_layout.hh"
#include "UI_resources.hh"




void draw_multiplayer_buttons(const bContext *, Panel *panel)
{
  uiBlock *block = panel->layout->block();

  uiDefBut(block,
           ButType::Label,
           0,
           IFACE_("Nothing selected TEST"),
           20,
           130,
           200,
           20,
           nullptr,
           0,
           0,
           "");


}

