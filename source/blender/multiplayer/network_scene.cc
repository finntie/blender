#include "network_scene.hh"

#include "BKE_action.hh"
#include "BKE_context.hh"
#include "BKE_customdata.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_library.hh"
#include "BKE_main.hh"
#include "BKE_main_namemap.hh"
#include "BKE_mesh.hh"
#include "BKE_node.hh"
#include "BKE_object.hh"
#include "BKE_scene.hh"
#include "BKE_screen.hh"

extern "C" {
#include "BKE_mesh.h"
#include "BKE_mesh_legacy_convert.hh"  // Some functions might be here
}

#include "BKE_mesh_runtime.hh"
#include "BKE_attribute.hh"
#include "BKE_mesh_mapping.hh"

#include "DNA_object_types.h"

#include "DNA_collection_types.h"

#include "ED_mesh.hh"

#include "WM_api.hh"
#include "WM_types.hh"

#include "BLI_array.hh"
#include "BLI_listbase.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"

#include <iostream>
#include <sstream>

namespace blender::multiplayer {

void MU_reset_scene(const bContext *C)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  BKE_view_layer_synced_ensure(scene, view_layer);

  blender::Vector<Object *> delete_objects;
  LISTBASE_FOREACH (CollectionObject *, cob, &view_layer->active_collection->collection->gobject) {
    delete_objects.append(cob->ob);
  }

  for (Object *ob : delete_objects) {
    BKE_id_delete(bmain, ob);
  }

  WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);
}

int MU_get_default_shape(Object *ob)
{
  if (ob->type == OB_MESH) {

    enum primitives {
      PRIM_PLANE,
      PRIM_CUBE,
      PRIM_CIRCLE,
      PRIM_UVSPHERE,
      PRIM_ICOSPHERE,
      PRIM_CYLINDER,
      PRIM_CONE,
      PRIM_TORUS,
      PRIM_MONKEY
    };
    primitives prim;
    Mesh *mesh = reinterpret_cast<Mesh *>(ob->data);
    int vnum = mesh->verts_num;
    int fnum = mesh->faces_num;
    int ednum = mesh->edges_num;

    if (vnum == 4 && fnum == 1 && ednum == 4) {
      prim = PRIM_CUBE;
    }
    else if (vnum == 8 && fnum == 6 && ednum == 12) {
      prim = PRIM_CUBE;
    }
    else if (vnum == 32 && fnum == 1 && ednum == 33) {
      prim = PRIM_CIRCLE;
    }
    else if (vnum == 64 && fnum == 34 && ednum == 99) {
      prim = PRIM_CYLINDER;
    }
    else {
      prim = PRIM_CUBE;
    }
    /* TODO: Possibly add more */

    return static_cast<int>(prim);
  }
  return -1;
}

std::string MU_object_to_message(Object *ob, const char *package_name)
{
  std::string message;
  std::stringstream ss;

  /* Give package a name. */
  /*TODO: ADD NAME*/
  ss << "Peer: " << package_name << " " << static_cast<int>(ob->type) << " " << (ob->id.name + 2)
     << " ";

  /* Check what type it is */
  switch (ob->type) {
    case OB_MESH: {

      Mesh *mesh = reinterpret_cast<Mesh *>(ob->data);

      /* If mesh, first add counts of all. */
      ss << mesh->vert_positions().size() << " ";
      ss << mesh->edges().size() << " ";
      ss << mesh->faces().size() << " ";
      ss << mesh->corner_verts().size() << " ";
      ss << mesh->corner_edges().size() << " ";

      Span<float3> positions = mesh->vert_positions();
      Span<int2> edges = mesh->edges();
      OffsetIndices<int> faces = mesh->faces();
      Span<int> corner_verts = mesh->corner_verts();
      Span<int> corner_edges = mesh->corner_edges();

      /* Add all vertex positions */
      int i = 0;
      for (const auto vertpos : positions) {
        i++;
        ss << vertpos.x << " " << vertpos.y << " " << vertpos.z << " ";
      }

      /* Debug */
      
        std::stringstream ss2(ss.str());
        std::string d;
        ss2 >> d >> d >> d >> d >> d >> d >> d >> d >> d;


        for (int j = 0; j < mesh->vert_positions().size(); j++) {
          float x, y, z;
          ss2 >> x >> y >> z;
          printf("Vertexpos[%d] = %f, %f, %f\n", j, x, y, z);
        }
        printf("Total of %d vertices\n", i);
      

      /* Add all edges */
      for (const auto edge : edges) {
        ss << edge.x << " " << edge.y << " ";
      }

      /* Debug */
      {
        i = 0;
        for (const auto edge : edges) {
          printf("Edges[%d] = %d, %d\n", i, edge.x, edge.y);
          i++;
        }
      }

      /* Add all faces */
      for (int i = 0; i < faces.size(); i++) {
        ss << faces[i].start() << " " << faces[i].size() << " ";
      }
      /* Add all corner vertices */
      for (const auto cornvert : corner_verts) {
        ss << cornvert << " ";
      }
      /* Add all corner edges */
      for (const auto cornedge : corner_edges) {
        ss << cornedge << " ";
      }

      message = ss.str();

      break;
    }
    case OB_LAMP: {

      break;
    }
    case OB_CAMERA: {

      break;
    }
    default:
      break;
  }

  return message;
}

void MU_message_to_object(bContext *C, std::string message)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);
  if (!bmain || !scene || !view_layer) {
    printf("Error getting bmain, scene or view_layer in network_scene.cc\n");
    return;
  }

  std::stringstream ss(message);
  //ss.str().reserve(1024 * 64); /*64 KB */
  //ss.str(message);


  /* first 2 values are useless values. */
  std::string useless;
  ss >> useless >> useless;

  /* Get the type. */
  int type;
  ss >> type;

  /* Get object name. */
  std::string name;
  ss >> name;

  if (BKE_main_global_namemap_contain_name(*bmain, ID_OB, name)) {
    printf("Object already has this name\n");
    return;
  }

  /* Get the counts. */
  int verts_num, edges_num, faces_num, corn_verts_num, corn_edges_num;
  ss >> verts_num >> edges_num >> faces_num >> corn_verts_num >> corn_edges_num;

  Mesh *mesh = BKE_mesh_new_nomain(verts_num, edges_num, faces_num, corn_verts_num);
  
  BKE_main_global_namemap_get_unique_name(*bmain, mesh->id, mesh->id.name);

  printf("Message size: %zu bytes\n", message.size());
  printf("Message content length: %zu chars\n", message.length());
  int space_count = std::count(message.begin(), message.end(), ' ');
  int expected_spaces = verts_num * 3 + edges_num * 2 + faces_num * 2 + corn_verts_num + corn_edges_num;
  printf("Spaces in message: %d, expected: %d\n", space_count, expected_spaces);

  blender::MutableSpan<blender::float3> verts = mesh->vert_positions_for_write();
  blender::MutableSpan<blender::int2> edge = mesh->edges_for_write();
  blender::MutableSpan<int> face = mesh->face_offsets_for_write();
  blender::MutableSpan<int> corn_verts = mesh->corner_verts_for_write();
  blender::MutableSpan<int> corn_edges = mesh->corner_edges_for_write();

  // for (int i = 0; i < verts_num; i++) {
  //  float x = 0;
  //   float y = 0;
  //  float z = 0;
  //   ss >> x >> y >> z;
  // 
  //  printf("  verts[%d] = %f, %f, %f\n", i, x, y, z);
  //  //ss >> verts[i].x >> verts[i].y >> verts[i].z;
  //}
  // for (int i = 0; i < edges_num; i++) {
  //   int x = 0;
  //   int y = 0;
  //   ss >> x >> y;

  //   printf("  verts[%d] = %d, %d\n", i, x, y);

  //   // ss >> verts[i].x >> verts[i].y >> verts[i].z;
  // }

  for (int i = 0; i < verts_num; i++) {
    ss >> verts[i].x >> verts[i].y >> verts[i].z;
  }

  for (int i = 0; i < edges_num; i++) {
    ss >> edge[i].x >> edge[i].y;

    if (edge[i][0] < 0 || edge[i][0] >= verts_num || edge[i][1] < 0 || edge[i][1] >= verts_num)
    {
      printf("Invalid edge indices: %d, %d\n", edge[i][0], edge[i][1]);
      return;
    }
  }

  face[0] = 0;
  int total_corners = 0;
  for (int i = 0; i < faces_num; i++) {
    int start, size;
    ss >> start >> size;

    if (size < 3) {  // Faces need at least 3 vertices
      printf("Invalid face size: %d\n", size);
      return;
    }
    total_corners += size;
    if (i + 1 < faces_num) {
      face[i + 1] = start + size;
    }
  }

  // Verify corner count matches
  if (total_corners != corn_verts_num) {
    printf("Corner count mismatch: expected %d, got %d\n", corn_verts_num, total_corners);
    return;
  }

  for (int i = 0; i < corn_verts_num; i++) {
    ss >> corn_verts[i];

      if (corn_verts[i] < 0 || corn_verts[i] >= verts_num) {
      printf("Invalid corner vertex index: %d\n", corn_verts[i]);
      return;
    }
  }
  for (int i = 0; i < corn_edges_num; i++) {
    ss >> corn_edges[i];

      if (corn_edges[i] < 0 || corn_edges[i] >= edges_num) {
      printf("Invalid corner edge index: %d\n", corn_edges[i]);
      return;
    }
  }
  
  if (ss.fail()) {
    printf("SS failed\n");
  }

  mesh->tag_positions_changed();
  mesh->tag_topology_changed();

  // Validate the mesh structure
  if (BKE_mesh_validate(mesh, true, true)) {
    printf("Change made to the mesh\n");
  }

 // BLI_addtail(&bmain->meshes, mesh);

  Object *ob = BKE_object_add_only_object(bmain, type, name.c_str());
  ob->data = mesh;
  //id_us_plus(&ob->id);

  BKE_collection_object_add(bmain, scene->master_collection, ob);
  BKE_view_layer_synced_ensure(scene, view_layer);

  WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, scene);
  DEG_id_tag_update(&ob->id, ID_RECALC_GEOMETRY);
  DEG_relations_tag_update(bmain);

  BKE_main_id_tag_all(bmain, ID_TAG_DOIT, false);

  printf("Object made\n");
}

}  // namespace blender::multiplayer
