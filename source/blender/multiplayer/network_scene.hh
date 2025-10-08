#pragma once

#include <string>

struct bContext;
struct Object;

namespace blender::multiplayer {

  void MU_reset_scene(const bContext *C);

  /* Check roughly what default shape the object is */
  int MU_get_default_shape(Object *ob);

  std::string MU_object_to_message(Object *ob, const char *package_name);

  void MU_message_to_object(bContext *C, std::string message);

  }
