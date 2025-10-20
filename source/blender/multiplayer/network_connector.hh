#pragma once

#include "network_base.hh"

struct bContext;
struct Object;
struct wmOperatorType;
namespace blender::multiplayer {

/**
 * Sends message
 *
 * \param poin: name of the property
 * \param poin2: nothing
 */
void MU_printRandomStatement(bContext *C, void *poin, void *poin2);



void MU_initialize_network_class(const bContext *C);

void MU_add_client_to_vector(bContext *C, void *poin, void *poin2);

blender::Vector<std::string> MU_get_clients_vector();

void MU_initialize_operator(wmOperatorType *OT);

void MU_host_same_device(bContext *C, void *poin, void *poin2);

void MU_connect_same_device(bContext *C, void *poin, void *poin2);

/* Called when object changes transform */
void MU_handle_transform(Object* ob);

/* Called when a layer is updated, includes when an object is added/removed */
void MU_layer_update(Object *ob);

/**
 * Callback function receiving package transform
 */
void MU_package_transform(const std::string& buffer);

void MU_package_update_object(const std::string &buffer);
}  // namespace blender::multiplayer
