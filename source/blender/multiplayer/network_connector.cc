#include "network_connector.hh"
#include "network_base.hh"
#include <iostream>

#include "BKE_context.hh"

#include "DNA_object_types.h"

namespace blender::multiplayer {

Dance MU_class_object;

void printRandomStatement(bContext *, void *poin, void *poin2)
{
  const char *message = static_cast<const char *>(poin2);
  printf("Message containing: %s, will be send:\n", message);

  if (MU_class_object.MU_get_if_host()) {
  MU_class_object.MU_send_message_to(std::string(message), 1, false, false);
  }
  else
  {
    MU_class_object.MU_send_message_to(std::string(message), 0, false, false);
  }
}

void initialize_network_class()
{
  MU_class_object.MU_init(false, false);
}

void host_same_device(bContext *, void *, void *)
{
  MU_class_object.MU_host(Dance::SAMEDEVICE, 10);
}

void connect_same_device(bContext *, void *, void *)
{
  MU_class_object.MU_connect(Dance::SAMEDEVICE, "192.168.0.2");
}

void MU_handle_transform(Object* ob)
{
  if (ob) {
    printf("New object pos is: %f, %f, %f\n", ob->loc[0], ob->loc[1], ob->loc[2]);
  }
  else {
    printf("Something went wrong casting the object\n");
  }
}

}  // namespace blender::multiplayer
