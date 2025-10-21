#pragma once


struct bContext;
struct Panel;

namespace blender::multiplayer {

void MU_panel_register(ARegionType *art);

void MU_draw_multiplayer_buttons(const bContext *C, Panel *panel);

}
