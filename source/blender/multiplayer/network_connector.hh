#pragma once

#include "network_base.hh"

struct bContext;
struct Object;
namespace blender::multiplayer {

/**
 * Sends message
 *
 * \param poin: name of the property
 * \param poin2: nothing
 */
void printRandomStatement(bContext *C, void *poin, void *poin2);

void initialize_network_class();

void host_same_device(bContext *C, void *poin, void *poin2);

void connect_same_device(bContext *C, void *poin, void *poin2);

void MU_handle_transform(Object* ob);

}  // namespace blender::multiplayer
