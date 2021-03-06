#include "../Defines.h"
#include "OBJLoader.h"
#include "../Resources/MeshResource.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "ext/tiny_obj_loader.h"

#include <experimental/filesystem>
#include <map>
#include <unordered_map>

MiniRM::Resource* OBJLoader::load(const char* path, const long GUID) {
	// Checking if the asset is in a package
	std::string filePath = path;
	size_t check = 0;
	check = filePath.find(".zip");
	bool loadZipped = false;
	if (check < filePath.length()) {
		loadZipped = true;
		filePath = extractFile(path, check);
	}

	// OBJ_Loader Library Variables
	/// ----------------------------------------
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warning;
	std::string error;
	/// ----------------------------------------

	// STEP 1: LOAD THE OBJ FILE
	/// ----------------------------------
	try {
		tinyobj::LoadObj(
			&attrib,
			&shapes,
			&materials,
			&warning,
			&error,
			filePath.c_str()
		);
	}
	catch (const std::exception& e) {
		RM_DEBUG_MESSAGE(e.what(), 1);
	}

	if (attrib.vertices.size() < 1)
		RM_DEBUG_MESSAGE("Couldn't find any vertex positions in " + std::string(path), 1);
	if (attrib.normals.size() < 1)
		RM_DEBUG_MESSAGE("Couldn't find any vertex normals in " + std::string(path), 0);
	if (attrib.texcoords.size() < 1)
		RM_DEBUG_MESSAGE("Couldn't find any vertex UVs in " + std::string(path), 0);

	uint32_t* indicesPtr;
	float* verticesDataPtr;

	unsigned int numIndices = 0;
	for (const auto& shape : shapes)
		numIndices += shape.mesh.indices.size();

	verticesDataPtr = RM_PLACEMENT_NEW(numIndices * 8 * sizeof(float), float);
	indicesPtr = RM_PLACEMENT_NEW(numIndices * sizeof(uint32_t), uint32_t);

	unsigned int i = 0;

	// Very inefficient way of drawing loading the meshes
	for (const auto& shape : shapes) // PER SHAPE
	{
		for (const auto& index : shape.mesh.indices) // PER INDEX (per shape)
		{
			// Vertex Index
			indicesPtr[i] = i;

			verticesDataPtr[i * 8 + 0] = (attrib.vertices[3 * index.vertex_index + 0]);
			verticesDataPtr[i * 8 + 1] = (attrib.vertices[3 * index.vertex_index + 1]);
			verticesDataPtr[i * 8 + 2] = (attrib.vertices[3 * index.vertex_index + 2]);

			// Check if there's normals data in file, else use faux data (Should calculate normals)
			if (attrib.normals.size() > 0) {
				verticesDataPtr[i * 8 + 3] = (attrib.normals[3 * index.normal_index + 0]);
				verticesDataPtr[i * 8 + 4] = (attrib.normals[3 * index.normal_index + 1]);
				verticesDataPtr[i * 8 + 5] = (attrib.normals[3 * index.normal_index + 2]);
			}
			else {
				verticesDataPtr[i * 8 + 3] = (1.f);
				verticesDataPtr[i * 8 + 4] = (1.f);
				verticesDataPtr[i * 8 + 5] = (0.f);
			}

			// Check if there's texCoord data in file, else use faux data
			if (attrib.texcoords.size() > 0) {
				verticesDataPtr[i * 8 + 6] = (attrib.texcoords[2 * index.texcoord_index + 0]);
				verticesDataPtr[i * 8 + 7] = (1.f - attrib.texcoords[2 * index.texcoord_index + 1]);
			}
			else {
				verticesDataPtr[i * 8 + 6] = (0.f);
				verticesDataPtr[i * 8 + 7] = (0.f);
			}

			i++;
		}
	}

	unsigned int sizeOnRAM = sizeof(MeshResource);
	// 8 floats per vertex and currently 1 vertex per index (simple loading)
	unsigned int verticesDataSize = numIndices * 8;
	MeshResource* meshToBeReturned = RM_PLACEMENT_NEW(sizeOnRAM, MeshResource(verticesDataPtr, indicesPtr, verticesDataSize, numIndices, GUID));
	// Size on DRAM
	meshToBeReturned->setSizeCPU(sizeOnRAM);
	// Size on VRAM
	meshToBeReturned->setSizeGPU(verticesDataSize * sizeof(float) + numIndices * sizeof(uint32_t));
	/// ----------------------------------------------------

	if (loadZipped) {
		// Deleting extracted file once loaded in to memory
		std::experimental::filesystem::remove(filePath.c_str());
	}

	return meshToBeReturned;
}
