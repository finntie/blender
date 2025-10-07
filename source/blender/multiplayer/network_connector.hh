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

void initialize_network_class(const bContext *C);

void host_same_device(bContext *C, void *poin, void *poin2);

void connect_same_device(bContext *C, void *poin, void *poin2);

void MU_handle_transform(Object* ob);

void reset_scene();

/**
 * Callback function receiving package transform
 */
void MU_package_transform(const std::string& buffer);
}  // namespace blender::multiplayer
