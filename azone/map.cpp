#include "map.h"
#include <sstream>
#include "compression.h"

#include <gtc/matrix_transform.hpp>

Map::Map() {
}

Map::~Map() {
}

bool Map::Build(std::string zone_name) {
	//try to load a v1-3 eqg here
	EQEmu::EQGLoader eqg;
	std::vector<std::shared_ptr<EQEmu::EQG::Geometry>> eqg_models;
	std::vector<std::shared_ptr<EQEmu::Placeable>> eqg_placables;
	std::vector<std::shared_ptr<EQEmu::EQG::Region>> eqg_regions;
	std::vector<std::shared_ptr<EQEmu::Light>> eqg_lights;
	if (eqg.Load(zone_name, eqg_models, eqg_placables, eqg_regions, eqg_lights)) {
		return CompileEQG(eqg_models, eqg_placables, eqg_regions, eqg_lights);
	}

	//if that fails try to load a v4 eqg here
	EQEmu::EQG4Loader eqg4;
	if (eqg4.Load(zone_name, terrain)) {
		return CompileEQGv4();
	}
	
	//if that fails try to load a s3d here
	EQEmu::S3DLoader s3d;
	std::vector<EQEmu::WLDFragment> zone_frags;
	std::vector<EQEmu::WLDFragment> zone_object_frags;
	std::vector<EQEmu::WLDFragment> object_frags;
	if (!s3d.ParseWLDFile(zone_name + ".s3d", zone_name + ".wld", zone_frags)) {
		return false;
	}

	if (!s3d.ParseWLDFile(zone_name + ".s3d", "objects.wld", zone_object_frags)) {
		return false;
	}

	if (!s3d.ParseWLDFile(zone_name + "_obj.s3d", zone_name + "_obj.wld", object_frags)) {
		return false;
	}

	return CompileS3D(zone_frags, zone_object_frags, object_frags);
}

bool Map::Write(std::string filename) {
	if ((collide_verts.size() == 0 || collide_indices.size() == 0 || non_collide_verts.size() == 0 || non_collide_indices.size() == 0) && !terrain)
		return false;

	FILE *f = fopen(filename.c_str(), "wb");

	if(!f)
		return false;
	
	uint32_t version = 0x02000000;
	if (fwrite(&version, sizeof(uint32_t), 1, f) != 1) {
		fclose(f);
		return false;
	}
	
	std::stringstream ss(std::stringstream::in | std::stringstream::out | std::stringstream::binary);
	uint32_t collide_vert_count = (uint32_t)collide_verts.size();
	uint32_t collide_ind_count = (uint32_t)collide_indices.size();
	uint32_t non_collide_vert_count = (uint32_t)non_collide_verts.size();
	uint32_t non_collide_ind_count = (uint32_t)non_collide_indices.size();
	uint32_t tile_count = terrain ? (uint32_t)terrain->GetTiles().size() : 0;
	uint32_t quads_per_tile = terrain ? terrain->GetQuadsPerTile() : 0;
	float units_per_vertex = terrain ? terrain->GetUnitsPerVertex() : 0.0f;
	
	ss.write((const char*)&collide_vert_count, sizeof(uint32_t));
	ss.write((const char*)&collide_ind_count, sizeof(uint32_t));
	ss.write((const char*)&non_collide_vert_count, sizeof(uint32_t));
	ss.write((const char*)&non_collide_ind_count, sizeof(uint32_t));
	ss.write((const char*)&tile_count, sizeof(uint32_t));
	ss.write((const char*)&quads_per_tile, sizeof(uint32_t));
	ss.write((const char*)&units_per_vertex, sizeof(float));

	for(uint32_t i = 0; i < collide_vert_count; ++i) {
		auto vert = collide_verts[i];
	
		ss.write((const char*)&vert.x, sizeof(float));
		ss.write((const char*)&vert.y, sizeof(float));
		ss.write((const char*)&vert.z, sizeof(float));
	}
	
	for (uint32_t i = 0; i < collide_ind_count; ++i) {
		uint32_t ind = collide_indices[i];
	
		ss.write((const char*)&ind, sizeof(uint32_t));
	}
	
	for(uint32_t i = 0; i < non_collide_vert_count; ++i) {
		auto vert = non_collide_verts[i];
	
		ss.write((const char*)&vert.x, sizeof(float));
		ss.write((const char*)&vert.y, sizeof(float));
		ss.write((const char*)&vert.z, sizeof(float));
	}
	
	for (uint32_t i = 0; i < non_collide_ind_count; ++i) {
		uint32_t ind = non_collide_indices[i];
	
		ss.write((const char*)&ind, sizeof(uint32_t));
	}

	if(terrain) {
		uint32_t quad_count = (quads_per_tile * quads_per_tile);
		uint32_t vert_count = ((quads_per_tile + 1) * (quads_per_tile + 1));
		auto &tiles = terrain->GetTiles();
		for (uint32_t i = 0; i < tile_count; ++i) {
			if(tiles[i]->IsFlat()) {
				bool flat = true;
				float x = tiles[i]->GetX();
				float y = tiles[i]->GetY();
				float z = tiles[i]->GetFloats()[0];
				ss.write((const char*)&flat, sizeof(bool));
				ss.write((const char*)&x, sizeof(float));
				ss.write((const char*)&y, sizeof(float));
				ss.write((const char*)&z, sizeof(float));
			} else {
				bool flat = false;
				float x = tiles[i]->GetX();
				float y = tiles[i]->GetY();
				ss.write((const char*)&flat, sizeof(bool));
				ss.write((const char*)&x, sizeof(float));
				ss.write((const char*)&y, sizeof(float));

				auto &flags = tiles[i]->GetFlags();
				for(uint32_t j = 0; j < quad_count; ++j) {
					uint8_t f = flags[j];
					ss.write((const char*)&f, sizeof(uint8_t));
				}

				auto &floats = tiles[i]->GetFloats();
				for (uint32_t j = 0; j < vert_count; ++j) {
					float f = floats[j];
					ss.write((const char*)&f, sizeof(float));
				}
			}
		}
	}
	
	std::vector<char> buffer;
	uint32_t buffer_len = (uint32_t)(ss.str().length() + 128);
	buffer.resize(buffer_len);

	uint32_t out_size = EQEmu::DeflateData(ss.str().c_str(), (uint32_t)ss.str().length(), &buffer[0], buffer_len);
	if (fwrite(&out_size, sizeof(uint32_t), 1, f) != 1) {
		fclose(f);
		return false;
	}
	
	uint32_t uncompressed_size = (uint32_t)ss.str().length();
	if (fwrite(&uncompressed_size, sizeof(uint32_t), 1, f) != 1) {
		fclose(f);
		return false;
	}


	if (fwrite(&buffer[0], out_size, 1, f) != 1) {
		fclose(f);
		return false;
	}

	fclose(f);
	return true;
}

void Map::TraverseBone(std::shared_ptr<EQEmu::SkeletonTrack::Bone> bone, glm::vec3 parent_trans, glm::vec3 parent_rot, glm::vec3 parent_scale)
{
	float offset_x = 0.0f;
	float offset_y = 0.0f;
	float offset_z = 0.0f;

	float rot_x = 0.0f;
	float rot_y = 0.0f;
	float rot_z = 0.0f;

	float scale_x = parent_scale.x;
	float scale_y = parent_scale.y;
	float scale_z = parent_scale.z;

	if(bone->orientation) {
		if (bone->orientation->shift_denom != 0) {
			offset_x = (float)bone->orientation->shift_x_num / (float)bone->orientation->shift_denom;
			offset_y = (float)bone->orientation->shift_y_num / (float)bone->orientation->shift_denom;
			offset_z = (float)bone->orientation->shift_z_num / (float)bone->orientation->shift_denom;
		}

		if(bone->orientation->rotate_denom != 0) {
			rot_x = (float)bone->orientation->rotate_x_num / (float)bone->orientation->rotate_denom;
			rot_y = (float)bone->orientation->rotate_y_num / (float)bone->orientation->rotate_denom;
			rot_z = (float)bone->orientation->rotate_z_num / (float)bone->orientation->rotate_denom;

			rot_x = rot_x *3.14159f / 180.0f;
			rot_y = rot_y *3.14159f / 180.0f;
			rot_z = rot_z *3.14159f / 180.0f;
		}
	}

	glm::vec3 pos(offset_x, offset_y, offset_z);
	glm::vec3 rot(rot_x, rot_y, rot_z);

	RotateVertex(pos, parent_rot.x, parent_rot.y, parent_rot.z);
	pos += parent_trans;
	
	rot += parent_rot;

	if(bone->model) {
		auto &mod_polys = bone->model->GetPolygons();
		auto &mod_verts = bone->model->GetVertices();

		for (uint32_t j = 0; j < mod_polys.size(); ++j) {
			auto &current_poly = mod_polys[j];
			auto v1 = mod_verts[current_poly.verts[0]];
			auto v2 = mod_verts[current_poly.verts[1]];
			auto v3 = mod_verts[current_poly.verts[2]];

			RotateVertex(v1.pos, rot.x, rot.y, rot.z);
			RotateVertex(v2.pos, rot.x, rot.y, rot.z);
			RotateVertex(v3.pos, rot.x, rot.y, rot.z);

			ScaleVertex(v1.pos, scale_x, scale_y, scale_z);
			ScaleVertex(v2.pos, scale_x, scale_y, scale_z);
			ScaleVertex(v3.pos, scale_x, scale_y, scale_z);

			TranslateVertex(v1.pos, pos.x, pos.y, pos.z);
			TranslateVertex(v2.pos, pos.x, pos.y, pos.z);
			TranslateVertex(v3.pos, pos.x, pos.y, pos.z);

#ifdef INVERSEXY
			float t = v1.pos.x;
			v1.pos.x = v1.pos.y;
			v1.pos.y = t;

			t = v2.pos.x;
			v2.pos.x = v2.pos.y;
			v2.pos.y = t;

			t = v3.pos.x;
			v3.pos.x = v3.pos.y;
			v3.pos.y = t;
#endif
			if (current_poly.flags == 0x10)
				AddFace(v1.pos, v2.pos, v3.pos, false);
			else
				AddFace(v1.pos, v2.pos, v3.pos, true);
		}
	}

	for(size_t i = 0; i < bone->children.size(); ++i) {
		TraverseBone(bone->children[i], pos, rot, parent_scale);
	}
}

bool Map::CompileS3D(
	std::vector<EQEmu::WLDFragment> &zone_frags,
	std::vector<EQEmu::WLDFragment> &zone_object_frags,
	std::vector<EQEmu::WLDFragment> &object_frags
	)
{
	collide_verts.clear();
	collide_indices.clear();
	non_collide_verts.clear();
	non_collide_indices.clear();
	current_collide_index = 0;
	current_non_collide_index = 0;
	collide_vert_to_index.clear();
	non_collide_vert_to_index.clear();

	for(uint32_t i = 0; i < zone_frags.size(); ++i) {
		if(zone_frags[i].type == 0x36) {
			EQEmu::WLDFragment36 &frag = reinterpret_cast<EQEmu::WLDFragment36&>(zone_frags[i]);
			auto model = frag.GetData();
		
			auto &mod_polys = model->GetPolygons();
			auto &mod_verts = model->GetVertices();

			for (uint32_t j = 0; j < mod_polys.size(); ++j) {
				auto &current_poly = mod_polys[j];
				auto v1 = mod_verts[current_poly.verts[0]];
				auto v2 = mod_verts[current_poly.verts[1]];
				auto v3 = mod_verts[current_poly.verts[2]];
#ifdef INVERSEXY
				float t = v1.pos.x;
				v1.pos.x = v1.pos.y;
				v1.pos.y = t;

				t = v2.pos.x;
				v2.pos.x = v2.pos.y;
				v2.pos.y = t;

				t = v3.pos.x;
				v3.pos.x = v3.pos.y;
				v3.pos.y = t;
#endif
				if(current_poly.flags == 0x10)
					AddFace(v1.pos, v2.pos, v3.pos, false);
				else
					AddFace(v1.pos, v2.pos, v3.pos, true);
			}
		}
	}

	std::vector<std::pair<std::shared_ptr<EQEmu::Placeable>, std::shared_ptr<EQEmu::Geometry>>> placables;
	std::vector<std::pair<std::shared_ptr<EQEmu::Placeable>, std::shared_ptr<EQEmu::SkeletonTrack>>> placables_skeleton;
	for (uint32_t i = 0; i < zone_object_frags.size(); ++i) {
		if (zone_object_frags[i].type == 0x15) {
			EQEmu::WLDFragment15 &frag = reinterpret_cast<EQEmu::WLDFragment15&>(zone_object_frags[i]);
			auto plac = frag.GetData();

			if(!plac)
			{
				printf("Warning: Placeable entry was not found...\n");
				continue;
			}

			bool found = false;
			for (uint32_t o = 0; o < object_frags.size(); ++o) {
				if (object_frags[o].type == 0x14) {
					EQEmu::WLDFragment14 &obj_frag = reinterpret_cast<EQEmu::WLDFragment14&>(object_frags[o]);
					auto mod_ref = obj_frag.GetData();

					if(mod_ref->GetName().compare(plac->GetName()) == 0) {
						found = true;

						auto &frag_refs = mod_ref->GetFrags();
						for (uint32_t m = 0; m < frag_refs.size(); ++m) {
							if (object_frags[frag_refs[m] - 1].type == 0x2D) {
								EQEmu::WLDFragment2D &r_frag = reinterpret_cast<EQEmu::WLDFragment2D&>(object_frags[frag_refs[m] - 1]);
								auto m_ref = r_frag.GetData();

								EQEmu::WLDFragment36 &mod_frag = reinterpret_cast<EQEmu::WLDFragment36&>(object_frags[m_ref]);
								auto mod = mod_frag.GetData();
								placables.push_back(std::make_pair(plac, mod));
							}
							else if (object_frags[frag_refs[m] - 1].type == 0x11) {
								EQEmu::WLDFragment11 &r_frag = reinterpret_cast<EQEmu::WLDFragment11&>(object_frags[frag_refs[m] - 1]);
								auto s_ref = r_frag.GetData();

								EQEmu::WLDFragment10 &skeleton_frag = reinterpret_cast<EQEmu::WLDFragment10&>(object_frags[s_ref]);
								auto skele = skeleton_frag.GetData();
								
								placables_skeleton.push_back(std::make_pair(plac, skele));
							}
						}

						break;
					}
				}
			}

			if(!found) {
				printf("Warning: could not find the model for placeable %s\n", plac->GetName().c_str());
			}
		}
	}

	size_t pl_sz = placables.size();
	for(size_t i = 0; i < pl_sz; ++i) {
		auto plac = placables[i].first;
		auto model = placables[i].second;

		auto &mod_polys = model->GetPolygons();
		auto &mod_verts = model->GetVertices();

		float offset_x = plac->GetX();
		float offset_y = plac->GetY();
		float offset_z = plac->GetZ();

		float rot_x = plac->GetRotateX() * 3.14159f / 180.0f;
		float rot_y = plac->GetRotateY() * 3.14159f / 180.0f;
		float rot_z = plac->GetRotateZ() * 3.14159f / 180.0f;

		float scale_x = plac->GetScaleX();
		float scale_y = plac->GetScaleY();
		float scale_z = plac->GetScaleZ();
		
		for (uint32_t j = 0; j < mod_polys.size(); ++j) {
			auto &current_poly = mod_polys[j];
			auto v1 = mod_verts[current_poly.verts[0]];
			auto v2 = mod_verts[current_poly.verts[1]];
			auto v3 = mod_verts[current_poly.verts[2]];

			RotateVertex(v1.pos, rot_x, rot_y, rot_z);
			RotateVertex(v2.pos, rot_x, rot_y, rot_z);
			RotateVertex(v3.pos, rot_x, rot_y, rot_z);

			ScaleVertex(v1.pos, scale_x, scale_y, scale_z);
			ScaleVertex(v2.pos, scale_x, scale_y, scale_z);
			ScaleVertex(v3.pos, scale_x, scale_y, scale_z);

			TranslateVertex(v1.pos, offset_x, offset_y, offset_z);
			TranslateVertex(v2.pos, offset_x, offset_y, offset_z);
			TranslateVertex(v3.pos, offset_x, offset_y, offset_z);

#ifdef INVERSEXY
			float t = v1.pos.x;
			v1.pos.x = v1.pos.y;
			v1.pos.y = t;

			t = v2.pos.x;
			v2.pos.x = v2.pos.y;
			v2.pos.y = t;

			t = v3.pos.x;
			v3.pos.x = v3.pos.y;
			v3.pos.y = t;
#endif
			if (current_poly.flags == 0x10)
				AddFace(v1.pos, v2.pos, v3.pos, false);
			else
				AddFace(v1.pos, v2.pos, v3.pos, true);
		}
	}

	pl_sz = placables_skeleton.size();
	for (size_t i = 0; i < pl_sz; ++i) {
		auto &plac = placables_skeleton[i].first;
		
		auto &bones = placables_skeleton[i].second->GetBones();

		if(bones.size() > 0) 
		{
			float offset_x = plac->GetX();
			float offset_y = plac->GetY();
			float offset_z = plac->GetZ();

			float rot_x = plac->GetRotateX() * 3.14159f / 180.0f;
			float rot_y = plac->GetRotateY() * 3.14159f / 180.0f;
			float rot_z = plac->GetRotateZ() * 3.14159f / 180.0f;

			float scale_x = plac->GetScaleX();
			float scale_y = plac->GetScaleY();
			float scale_z = plac->GetScaleZ();
			TraverseBone(bones[0], glm::vec3(offset_x, offset_y, offset_z), glm::vec3(rot_x, rot_y, rot_z), glm::vec3(scale_x, scale_y, scale_z));
		}
	}

	return true;
}

bool Map::CompileEQG(
	std::vector<std::shared_ptr<EQEmu::EQG::Geometry>> &models,
	std::vector<std::shared_ptr<EQEmu::Placeable>> &placeables,
	std::vector<std::shared_ptr<EQEmu::EQG::Region>> &regions,
	std::vector<std::shared_ptr<EQEmu::Light>> &lights
	)
{
	collide_verts.clear();
	collide_indices.clear();
	non_collide_verts.clear();
	non_collide_indices.clear();
	current_collide_index = 0;
	current_non_collide_index = 0;
	collide_vert_to_index.clear();
	non_collide_vert_to_index.clear();

	for(uint32_t i = 0; i < placeables.size(); ++i) {
		std::shared_ptr<EQEmu::Placeable> &plac = placeables[i];
		std::shared_ptr<EQEmu::EQG::Geometry> model;
		bool is_ter = false;

		if(plac->GetName().substr(0, 3).compare("TER") == 0)
			is_ter = true;

		for(uint32_t j = 0; j < models.size(); ++j) {
			if(models[j]->GetName().compare(plac->GetFileName()) == 0) {
				model = models[j];
				break;
			}
		}

		if (!model) {
			printf("Warning: could not find eqg placeable %s\n", plac->GetFileName().c_str());
			continue;
		}

		auto &mod_polys = model->GetPolygons();
		auto &mod_verts = model->GetVertices();

		float offset_x = plac->GetX();
		float offset_y = plac->GetY();
		float offset_z = plac->GetZ();

		float rot_x = plac->GetRotateX() * 3.14159f / 180.0f;
		float rot_y = plac->GetRotateY() * 3.14159f / 180.0f;
		float rot_z = plac->GetRotateZ() * 3.14159f / 180.0f;

		float scale_x = plac->GetScaleX();
		float scale_y = plac->GetScaleY();
		float scale_z = plac->GetScaleZ();

		for (uint32_t j = 0; j < mod_polys.size(); ++j) {
			auto &current_poly = mod_polys[j];
			auto v1 = mod_verts[current_poly.verts[0]];
			auto v2 = mod_verts[current_poly.verts[1]];
			auto v3 = mod_verts[current_poly.verts[2]];

			if(!is_ter) {
				RotateVertex(v1.pos, rot_x, rot_y, rot_z);
				RotateVertex(v2.pos, rot_x, rot_y, rot_z);
				RotateVertex(v3.pos, rot_x, rot_y, rot_z);

				ScaleVertex(v1.pos, scale_x, scale_y, scale_z);
				ScaleVertex(v2.pos, scale_x, scale_y, scale_z);
				ScaleVertex(v3.pos, scale_x, scale_y, scale_z);

				TranslateVertex(v1.pos, offset_x, offset_y, offset_z);
				TranslateVertex(v2.pos, offset_x, offset_y, offset_z);
				TranslateVertex(v3.pos, offset_x, offset_y, offset_z);
			}

#ifdef INVERSEXY
			float t = v1.pos.x;
			v1.pos.x = v1.pos.y;
			v1.pos.y = t;

			t = v2.pos.x;
			v2.pos.x = v2.pos.y;
			v2.pos.y = t;

			t = v3.pos.x;
			v3.pos.x = v3.pos.y;
			v3.pos.y = t;
#endif

			if (current_poly.flags & 0x01)
				AddFace(v1.pos, v2.pos, v3.pos, false);
			else
				AddFace(v1.pos, v2.pos, v3.pos, true);
		}
	}

	return true;
}

bool Map::CompileEQGv4()
{
	collide_verts.clear();
	collide_indices.clear();
	non_collide_verts.clear();
	non_collide_indices.clear();
	current_collide_index = 0;
	current_non_collide_index = 0;
	collide_vert_to_index.clear();
	non_collide_vert_to_index.clear();
	return true;
}

void Map::AddFace(glm::vec3 &v1, glm::vec3 &v2, glm::vec3 &v3, bool collidable) {
	if (!collidable) {
		std::tuple<float, float, float> tt = std::make_tuple(v1.x, v1.y, v1.z);
		auto iter = non_collide_vert_to_index.find(tt);
		if (iter == non_collide_vert_to_index.end()) {
			non_collide_vert_to_index[tt] = current_non_collide_index;
			non_collide_verts.push_back(v1);
			non_collide_indices.push_back(current_non_collide_index);

			++current_non_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			non_collide_indices.push_back(t_idx);
		}

		tt = std::make_tuple(v2.x, v2.y, v2.z);
		iter = non_collide_vert_to_index.find(tt);
		if (iter == non_collide_vert_to_index.end()) {
			non_collide_vert_to_index[tt] = current_non_collide_index;
			non_collide_verts.push_back(v2);
			non_collide_indices.push_back(current_non_collide_index);

			++current_non_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			non_collide_indices.push_back(t_idx);
		}

		tt = std::make_tuple(v3.x, v3.y, v3.z);
		iter = non_collide_vert_to_index.find(tt);
		if (iter == non_collide_vert_to_index.end()) {
			non_collide_vert_to_index[tt] = current_non_collide_index;
			non_collide_verts.push_back(v3);
			non_collide_indices.push_back(current_non_collide_index);

			++current_non_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			non_collide_indices.push_back(t_idx);
		}
	}
	else {
		std::tuple<float, float, float> tt = std::make_tuple(v1.x, v1.y, v1.z);
		auto iter = collide_vert_to_index.find(tt);
		if (iter == collide_vert_to_index.end()) {
			collide_vert_to_index[tt] = current_collide_index;
			collide_verts.push_back(v1);
			collide_indices.push_back(current_collide_index);

			++current_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			collide_indices.push_back(t_idx);
		}

		tt = std::make_tuple(v2.x, v2.y, v2.z);
		iter = collide_vert_to_index.find(tt);
		if (iter == collide_vert_to_index.end()) {
			collide_vert_to_index[tt] = current_collide_index;
			collide_verts.push_back(v2);
			collide_indices.push_back(current_collide_index);

			++current_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			collide_indices.push_back(t_idx);
		}

		tt = std::make_tuple(v3.x, v3.y, v3.z);
		iter = collide_vert_to_index.find(tt);
		if (iter == collide_vert_to_index.end()) {
			collide_vert_to_index[tt] = current_collide_index;
			collide_verts.push_back(v3);
			collide_indices.push_back(current_collide_index);

			++current_collide_index;
		}
		else {
			uint32_t t_idx = iter->second;
			collide_indices.push_back(t_idx);
		}
	}
}

void Map::RotateVertex(glm::vec3 &v, float rx, float ry, float rz) {
	glm::vec3 nv = v;

	nv.y = (cos(rx) * v.y) - (sin(rx) * v.z);
	nv.z = (sin(rx) * v.y) + (cos(rx) * v.z);

	v = nv;
	
	nv.x = (cos(ry) * v.x) + (sin(ry) * v.z);
	nv.z = -(sin(ry) * v.x) + (cos(ry) * v.z);

	v = nv;

	nv.x = (cos(rz) * v.x) - (sin(rz) * v.y);
	nv.y = (sin(rz) * v.x) + (cos(rz) * v.y);

	v = nv;
}

void Map::ScaleVertex(glm::vec3 &v, float sx, float sy, float sz) {
	v.x = v.x * sx;
	v.y = v.y * sy;
	v.z = v.z * sz;
}

void Map::TranslateVertex(glm::vec3 &v, float tx, float ty, float tz) {
	v.x = v.x + tx;
	v.y = v.y + ty;
	v.z = v.z + tz;
}
