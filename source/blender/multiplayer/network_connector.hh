#pragma once

#include "network_base.hh"

struct bContext;
struct Object;
struct wmOperatorType;
namespace blender::multiplayer {

void MU_initialize_network_class(const bContext *C);

void MU_add_client_to_vector(bContext *C, void *poin, void *poin2);

void MU_remove_client_from_vector(bContext *, void *poin, void *);

std::string MU_get_own_private_IP();
std::string MU_get_own_public_IP();

void MU_force_IPV4_button(bContext *, void *poin, void *);

void MU_private_IP_to_clipboard(bContext *, void *, void *);
void MU_public_IP_to_clipboard(bContext *, void *, void *);

blender::Vector<std::string> MU_get_clients_vector();

void MU_host(bContext *C, void *poin, void *poin2);

void MU_connect(bContext *C, void *poin, void *poin2);

/* Called when object changes transform */
void MU_handle_transform(Object* ob);

/* Called when a layer is updated, includes when an object is added/removed */
void MU_layer_update(Object *ob);

/**
 * Callback function receiving package transform
 */
void MU_package_transform(const std::string& buffer);

void MU_package_update_object(const std::string &buffer);

void MU_timer_operator(wmOperatorType *OT);


}  // namespace blender::multiplayer
