#include "BspMerger.h"
#include <algorithm>
#include "vis.h"

BspMerger::BspMerger() {

}

Bsp* BspMerger::merge(vector<Bsp*> maps, vec3 gap) {
	vector<vector<vector<MAPBLOCK>>> blocks = separate(maps, gap);

	printf("Arranging maps so that they don't overlap:\n");

	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (block.offset.x != 0 || block.offset.y != 0 || block.offset.z != 0) {
					printf("    Move %s by (%.0f, %.0f, %.0f)", block.map->name.c_str(), 
						block.offset.x, block.offset.y, block.offset.z);
					block.map->move(block.offset);
				}
			}
		}
	}

	// Merge order matters. 
	// The bounding box of a merged map is expanded to contain both maps, and bounding boxes cannot overlap.
	// TODO: Don't merge linearly. Merge gradually bigger chunks to minimize BSP tree depth.
	//       Not worth it until more than 27 maps are merged together (merge cube bigger than 3x3x3)

	printf("Merging %d maps:\n", maps.size());

	// merge maps along X axis to form rows of maps
	int rowId = 0;
	int mergeCount = 0;
	for (int z = 0; z < blocks.size(); z++) {
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& rowStart = blocks[z][y][0];
			for (int x = 0; x < blocks[z][y].size(); x++) {
				MAPBLOCK& block = blocks[z][y][x];

				if (x != 0) {
					//printf("Merge %d,%d,%d -> %d,%d,%d\n", x, y, z, 0, y, z);
					merge(rowStart, block, "row_" + to_string(rowId));
				}
			}
			rowId++;
		}
	}

	// merge the rows along the Y axis to form layers of maps
	int colId = 0;
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& colStart = blocks[z][0][0];
		for (int y = 0; y < blocks[z].size(); y++) {
			MAPBLOCK& block = blocks[z][y][0];

			if (y != 0) {
				//printf("Merge %d,%d,%d -> %d,%d,%d\n", 0, y, z, 0, 0, z);
				merge(colStart, block, "layer_" + to_string(colId));
			}
		}
		colId++;
	}

	// merge the layers to form a cube of maps
	MAPBLOCK& layerStart = blocks[0][0][0];
	for (int z = 0; z < blocks.size(); z++) {
		MAPBLOCK& block = blocks[z][0][0];

		if (z != 0) {
			//printf("Merge %d,%d,%d -> %d,%d,%d\n", 0, 0, z, 0, 0, 0);
			merge(layerStart, block, "result");
		}
	}

	return layerStart.map;
}

void BspMerger::merge(MAPBLOCK& dst, MAPBLOCK& src, string resultType) {
	string thisName = dst.merge_name.size() ? dst.merge_name : dst.map->name;
	string otherName = src.merge_name.size() ? src.merge_name : src.map->name;
	dst.merge_name = resultType;
	printf("    %-8s = %s + %s\n", dst.merge_name.c_str(), thisName.c_str(), otherName.c_str());

	merge(*dst.map, *src.map);
}

vector<vector<vector<MAPBLOCK>>> BspMerger::separate(vector<Bsp*>& maps, vec3 gap) {
	vector<MAPBLOCK> blocks;

	vector<vector<vector<MAPBLOCK>>> orderedBlocks;

	vec3 maxDims = vec3(0, 0, 0);
	for (int i = 0; i < maps.size(); i++) {
		MAPBLOCK block;
		maps[i]->get_bounding_box(block.mins, block.maxs);

		block.size = block.maxs - block.mins;
		block.offset = vec3(0, 0, 0);
		block.map = maps[i];
	

		if (block.size.x > maxDims.x) {
			maxDims.x = block.size.x;
		}
		if (block.size.y > maxDims.y) {
			maxDims.y = block.size.y;
		}
		if (block.size.z > maxDims.z) {
			maxDims.z = block.size.z;
		}

		blocks.push_back(block);
	}

	bool noOverlap = true;
	for (int i = 0; i < blocks.size() && noOverlap; i++) {
		for (int k = i + i; k < blocks.size(); k++) {
			if (blocks[i].intersects(blocks[k])) {
				noOverlap = false;
				break;
			}
		}
	}

	if (noOverlap) {
		printf("Maps do not overlap. They will be merged without moving.");
		return orderedBlocks;
	}

	maxDims += gap;

	int maxMapsPerRow = (MAX_MAP_COORD * 2.0f) / maxDims.x;
	int maxMapsPerCol = (MAX_MAP_COORD * 2.0f) / maxDims.y;
	int maxMapsPerLayer = (MAX_MAP_COORD * 2.0f) / maxDims.z;

	int idealMapsPerAxis = floor(pow(maps.size(), 1 / 3.0f));
	
	if (idealMapsPerAxis * idealMapsPerAxis * idealMapsPerAxis < maps.size()) {
		idealMapsPerAxis++;
	}

	if (maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer < maps.size()) {
		printf("Not enough space to merge all maps! Try moving them individually before merging.");
		return orderedBlocks;
	}

	vec3 mergedMapSize = maxDims * (float)idealMapsPerAxis;
	vec3 mergedMapMin = maxDims * -0.5f;

	printf("Max map size: %.0f %.0f %.0f\n", maxDims.x, maxDims.y, maxDims.z);
	printf("Max maps per axis: x=%d y=%d z=%d\n", maxMapsPerRow, maxMapsPerCol, maxMapsPerLayer);
	printf("Max maps of this size: %d\n", maxMapsPerRow * maxMapsPerCol * maxMapsPerLayer);

	int actualWidth = min(idealMapsPerAxis, (int)maps.size());
	int actualLength = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis)));
	int actualHeight = min(idealMapsPerAxis, (int)ceil(maps.size() / (float)(idealMapsPerAxis*idealMapsPerAxis)));
	printf("Merged map dimensions: %dx%dx%d maps\n", actualWidth, actualLength, actualHeight);

	vec3 targetMins = mergedMapMin;
	int blockIdx = 0;
	for (int z = 0; z < idealMapsPerAxis && blockIdx < blocks.size(); z++) {
		targetMins.y = -mergedMapMin.y;
		vector<vector<MAPBLOCK>> col;
		for (int y = 0; y < idealMapsPerAxis && blockIdx < blocks.size(); y++) {
			targetMins.x = -mergedMapMin.x;
			vector<MAPBLOCK> row;
			for (int x = 0; x < idealMapsPerAxis && blockIdx < blocks.size(); x++) {
				MAPBLOCK& block = blocks[blockIdx];

				block.offset = targetMins - block.mins;
				//printf("block %d: %.0f %.0f %.0f\n", blockIdx, targetMins.x, targetMins.y, targetMins.z);
				//printf("%s offset: %.0f %.0f %.0f\n", block.map->name.c_str(), block.offset.x, block.offset.y, block.offset.z);

				row.push_back(block);

				blockIdx++;
				targetMins.x += maxDims.x;
			}
			col.push_back(row);
			targetMins.y += maxDims.y;
		}
		orderedBlocks.push_back(col);
		targetMins.z += maxDims.z;
	}

	return orderedBlocks;
}

bool BspMerger::merge(Bsp& mapA, Bsp& mapB) {
	// TODO: Create a new map and store result there. Don't break mapA.

	last_progress = std::chrono::system_clock::now();

	BSPPLANE separationPlane = separate(mapA, mapB);
	if (separationPlane.nType == -1) {
		printf("No separating axis found. The maps overlap and can't be merged.\n");
		return false;
	}

	thisWorldLeafCount = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])->nVisLeafs; // excludes solid leaf 0
	otherWorldLeafCount = ((BSPMODEL*)mapB.lumps[LUMP_MODELS])->nVisLeafs; // excluding solid leaf 0

	texRemap.clear();
	texInfoRemap.clear();
	planeRemap.clear();
	leavesRemap.clear();
	modelLeafRemap.clear();

	bool shouldMerge[HEADER_LUMPS] = { false };

	for (int i = 0; i < HEADER_LUMPS; i++) {

		if (i == LUMP_VISIBILITY || i == LUMP_LIGHTING) {
			continue; // always merge
		}

		if (!mapA.lumps[i] && !mapB.lumps[i]) {
			//cout << "Skipping " << g_lump_names[i] << " lump (missing from both maps)\n";
		}
		else if (!mapA.lumps[i]) {
			cout << "Replacing " << g_lump_names[i] << " lump\n";
			mapA.header.lump[i].nLength = mapB.header.lump[i].nLength;
			mapA.lumps[i] = new byte[mapB.header.lump[i].nLength];
			memcpy(mapA.lumps[i], mapB.lumps[i], mapB.header.lump[i].nLength);

			// process the lump here (TODO: faster to just copy wtv needs copying)
			switch (i) {
			case LUMP_ENTITIES:
				mapA.load_ents(); break;
			}
		}
		else if (!mapB.lumps[i]) {
			cout << "Keeping " << g_lump_names[i] << " lump\n";
		}
		else {
			//cout << "Merging " << g_lump_names[i] << " lump\n";

			shouldMerge[i] = true;
		}
	}

	// base structures (they don't reference any other structures)
	if (shouldMerge[LUMP_ENTITIES])
		merge_ents(mapA, mapB);
	if (shouldMerge[LUMP_PLANES])
		merge_planes(mapA, mapB);
	if (shouldMerge[LUMP_TEXTURES])
		merge_textures(mapA, mapB);
	if (shouldMerge[LUMP_VERTICES])
		merge_vertices(mapA, mapB);

	if (shouldMerge[LUMP_EDGES])
		merge_edges(mapA, mapB); // references verts

	if (shouldMerge[LUMP_SURFEDGES])
		merge_surfedges(mapA, mapB); // references edges

	if (shouldMerge[LUMP_TEXINFO])
		merge_texinfo(mapA, mapB); // references textures

	if (shouldMerge[LUMP_FACES])
		merge_faces(mapA, mapB); // references planes, surfedges, and texinfo

	if (shouldMerge[LUMP_MARKSURFACES])
		merge_marksurfs(mapA, mapB); // references faces

	if (shouldMerge[LUMP_LEAVES])
		merge_leaves(mapA, mapB); // references vis data, and marksurfs

	if (shouldMerge[LUMP_NODES]) {
		create_merge_headnodes(mapA, mapB, separationPlane);
		merge_nodes(mapA, mapB);
		merge_clipnodes(mapA, mapB);
	}

	if (shouldMerge[LUMP_MODELS])
		merge_models(mapA, mapB);

	merge_lighting(mapA, mapB);

	// doing this last because it takes way longer than anything else, and limit overflows should fail the
	// merge as soon as possible.
	merge_vis(mapA, mapB);

	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("                               ");
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");

	return true;
}

BSPPLANE BspMerger::separate(Bsp& mapA, Bsp& mapB) {
	BSPMODEL& thisWorld = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])[0];
	BSPMODEL& otherWorld = ((BSPMODEL*)mapB.lumps[LUMP_MODELS])[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	BSPPLANE separationPlane;
	memset(&separationPlane, 0, sizeof(BSPPLANE));

	// separating plane points toward the other map (b)
	if (bmin.x >= amax.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { 1, 0, 0 };
		separationPlane.fDist = amax.x + (bmin.x - amax.x) * 0.5f;
	}
	else if (bmax.x <= amin.x) {
		separationPlane.nType = PLANE_X;
		separationPlane.vNormal = { -1, 0, 0 };
		separationPlane.fDist = bmax.x + (amin.x - bmax.x) * 0.5f;
	}
	else if (bmin.y >= amax.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, 1, 0 };
		separationPlane.fDist = bmin.y;
	}
	else if (bmax.y <= amin.y) {
		separationPlane.nType = PLANE_Y;
		separationPlane.vNormal = { 0, -1, 0 };
		separationPlane.fDist = bmax.y;
	}
	else if (bmin.z >= amax.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, 1 };
		separationPlane.fDist = bmin.z;
	}
	else if (bmax.z <= amin.z) {
		separationPlane.nType = PLANE_Z;
		separationPlane.vNormal = { 0, 0, -1 };
		separationPlane.fDist = bmax.z;
	}
	else {
		separationPlane.nType = -1; // no simple separating axis

		printf("Bounding boxes for each map:\n");
		printf("(%6.0f, %6.0f, %6.0f)", amin.x, amin.y, amin.z);
		printf(" - (%6.0f, %6.0f, %6.0f) %s\n", amax.x, amax.y, amax.z, mapA.name.c_str());

		printf("(%6.0f, %6.0f, %6.0f)", bmin.x, bmin.y, bmin.z);
		printf(" - (%6.0f, %6.0f, %6.0f) %s\n", bmax.x, bmax.y, bmax.z, mapB.name.c_str());
	}

	return separationPlane;
}

int BspMerger::getMipTexDataSize(int width, int height) {
	int sz = 256 * 3 + 4; // pallette + padding

	for (int i = 0; i < MIPLEVELS; i++) {
		sz += (width >> i)* (height >> i);
	}

	return sz;
}

void BspMerger::merge_ents(Bsp& mapA, Bsp& mapB)
{
	progress_title = "entities";
	progress = 0;
	progress_total = mapA.ents.size() + mapB.ents.size();

	int oldEntCount = mapA.ents.size();

	// update model indexes since this map's models will be appended after the other map's models
	int otherModelCount = (mapB.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL)) - 1;
	for (int i = 0; i < mapA.ents.size(); i++) {
		if (!mapA.ents[i]->hasKey("model") || mapA.ents[i]->keyvalues["model"][0] != '*') {
			continue;
		}
		string modelIdxStr = mapA.ents[i]->keyvalues["model"].substr(1);

		if (!isNumeric(modelIdxStr)) {
			continue;
		}

		int newModelIdx = atoi(modelIdxStr.c_str()) + otherModelCount;
		mapA.ents[i]->keyvalues["model"] = "*" + to_string(newModelIdx);

		print_merge_progress();
	}

	for (int i = 0; i < mapB.ents.size(); i++) {
		if (mapB.ents[i]->keyvalues["classname"] == "worldspawn") {
			Entity* otherWorldspawn = mapB.ents[i];

			vector<string> otherWads = splitString(otherWorldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < otherWads.size(); j++) {
				otherWads[j] = basename(otherWads[j]);
			}

			Entity* worldspawn = NULL;
			for (int k = 0; k < mapA.ents.size(); k++) {
				if (mapA.ents[k]->keyvalues["classname"] == "worldspawn") {
					worldspawn = mapA.ents[k];
					break;
				}
			}

			// merge wad list
			vector<string> thisWads = splitString(worldspawn->keyvalues["wad"], ";");

			// strip paths from wad names
			for (int j = 0; j < thisWads.size(); j++) {
				thisWads[j] = basename(thisWads[j]);
			}

			// add unique wads to this map
			for (int j = 0; j < otherWads.size(); j++) {
				if (std::find(thisWads.begin(), thisWads.end(), otherWads[j]) == thisWads.end()) {
					thisWads.push_back(otherWads[j]);
				}
			}

			worldspawn->keyvalues["wad"] = "";
			for (int j = 0; j < thisWads.size(); j++) {
				worldspawn->keyvalues["wad"] += thisWads[j] + ";";
			}

			// include prefixed version of the other maps keyvalues
			for (auto it = otherWorldspawn->keyvalues.begin(); it != otherWorldspawn->keyvalues.end(); it++) {
				if (it->first == "classname" || it->first == "wad") {
					continue;
				}
				// TODO: unknown keyvalues crash the game? Try something else.
				//worldspawn->addKeyvalue(Keyvalue(mapB.name + "_" + it->first, it->second));
			}
		}
		else {
			Entity* copy = new Entity();
			copy->keyvalues = mapB.ents[i]->keyvalues;
			copy->keyOrder = mapB.ents[i]->keyOrder;
			mapA.ents.push_back(copy);
		}

		print_merge_progress();
	}

	mapA.update_ent_lump();

	//cout << oldEntCount << " -> " << ents.size() << endl;
}

void BspMerger::merge_planes(Bsp& mapA, Bsp& mapB) {
	BSPPLANE* thisPlanes = (BSPPLANE*)mapA.lumps[LUMP_PLANES];
	BSPPLANE* otherPlanes = (BSPPLANE*)mapB.lumps[LUMP_PLANES];
	int numThisPlanes = mapA.header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);
	int numOtherPlanes = mapB.header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	progress_title = "planes";
	progress = 0;
	progress_total = numThisPlanes + numOtherPlanes;

	vector<BSPPLANE> mergedPlanes;
	mergedPlanes.reserve(numThisPlanes + numOtherPlanes);

	for (int i = 0; i < numThisPlanes; i++) {
		mergedPlanes.push_back(thisPlanes[i]);
		print_merge_progress();
	}
	for (int i = 0; i < numOtherPlanes; i++) {
		bool isUnique = true;
		for (int k = 0; k < numThisPlanes; k++) {
			if (memcmp(&otherPlanes[i], &thisPlanes[k], sizeof(BSPPLANE)) == 0) {
				isUnique = false;
				planeRemap.push_back(k);
				break;
			}
		}
		if (isUnique) {
			planeRemap.push_back(mergedPlanes.size());
			mergedPlanes.push_back(otherPlanes[i]);
		}

		print_merge_progress();
	}

	int newLen = mergedPlanes.size() * sizeof(BSPPLANE);
	int duplicates = (numThisPlanes + numOtherPlanes) - mergedPlanes.size();

	delete[] mapA.lumps[LUMP_PLANES];
	mapA.lumps[LUMP_PLANES] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_PLANES], &mergedPlanes[0], newLen);
	mapA.header.lump[LUMP_PLANES].nLength = newLen;
}

void BspMerger::merge_textures(Bsp& mapA, Bsp& mapB) {
	int32_t thisTexCount = *((int32_t*)(mapA.lumps[LUMP_TEXTURES]));
	int32_t otherTexCount = *((int32_t*)(mapB.lumps[LUMP_TEXTURES]));
	byte* thisTex = mapA.lumps[LUMP_TEXTURES];
	byte* otherTex = mapB.lumps[LUMP_TEXTURES];

	uint32_t newTexCount = 0;

	// temporary buffer for holding miptex + embedded textures (too big but doesn't matter)
	uint maxMipTexDataSize = mapA.header.lump[LUMP_TEXTURES].nLength + mapB.header.lump[LUMP_TEXTURES].nLength;
	byte* newMipTexData = new byte[maxMipTexDataSize];

	byte* mipTexWritePtr = newMipTexData;

	// offsets relative to the start of the mipmap data, not the lump
	uint32_t* mipTexOffsets = new uint32_t[thisTexCount + otherTexCount];

	progress_title = "planes";
	progress = 0;
	progress_total = thisTexCount + otherTexCount;

	uint thisMergeSz = (thisTexCount + 1) * 4;
	for (int i = 0; i < thisTexCount; i++) {
		int32_t offset = ((int32_t*)thisTex)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(thisTex + offset);

		int sz = sizeof(BSPMIPTEX);
		if (tex->nOffsets[0] != 0) {
			sz += getMipTexDataSize(tex->nWidth, tex->nHeight);
		}
		//memset(tex->nOffsets, 0, sizeof(uint32) * 4);

		mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
		memcpy(mipTexWritePtr, tex, sz);
		mipTexWritePtr += sz;
		newTexCount++;
		thisMergeSz += sz;

		print_merge_progress();
	}

	uint otherMergeSz = (otherTexCount + 1) * 4;
	for (int i = 0; i < otherTexCount; i++) {
		int32_t offset = ((int32_t*)otherTex)[i + 1];
		BSPMIPTEX* tex = (BSPMIPTEX*)(otherTex + offset);

		int sz = sizeof(BSPMIPTEX);
		if (tex->nOffsets[0] != 0) {
			sz += getMipTexDataSize(tex->nWidth, tex->nHeight);
		}

		bool isUnique = true;
		for (int k = 0; k < thisTexCount; k++) {
			BSPMIPTEX* thisTex = (BSPMIPTEX*)(newMipTexData + mipTexOffsets[k]);
			if (memcmp(tex, thisTex, sz) == 0 && false) {
				isUnique = false;
				texRemap.push_back(k);
				break;
			}
		}

		if (isUnique) {
			mipTexOffsets[newTexCount] = (mipTexWritePtr - newMipTexData);
			if (mipTexOffsets[newTexCount] > maxMipTexDataSize) {
				printf("ZOMG OVERFLOW\n");
			}

			texRemap.push_back(newTexCount);
			memcpy(mipTexWritePtr, tex, sz);
			mipTexWritePtr += sz;
			newTexCount++;
			otherMergeSz += sz;
		}

		print_merge_progress();
	}

	int duplicates = newTexCount - (thisTexCount + otherTexCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate textures\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	uint texHeaderSize = (newTexCount + 1) * sizeof(int32_t);
	uint newLen = (mipTexWritePtr - newMipTexData) + texHeaderSize;
	delete[] mapA.lumps[LUMP_TEXTURES];
	mapA.lumps[LUMP_TEXTURES] = new byte[newLen];

	// write texture lump header
	uint32_t* texHeader = (uint32_t*)(mapA.lumps[LUMP_TEXTURES]);
	texHeader[0] = newTexCount;
	for (int i = 0; i < newTexCount; i++) {
		texHeader[i + 1] = mipTexOffsets[i] + texHeaderSize;
	}

	memcpy(mapA.lumps[LUMP_TEXTURES] + texHeaderSize, newMipTexData, mipTexWritePtr - newMipTexData);
	mapA.header.lump[LUMP_TEXTURES].nLength = newLen;

	//cout << thisTexCount << " -> " << newTexCount << endl;
}

void BspMerger::merge_vertices(Bsp& mapA, Bsp& mapB) {
	vec3* thisVerts = (vec3*)mapA.lumps[LUMP_VERTICES];
	vec3* otherVerts = (vec3*)mapB.lumps[LUMP_VERTICES];
	thisVertCount = mapA.header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int otherVertCount = mapB.header.lump[LUMP_VERTICES].nLength / sizeof(vec3);
	int totalVertCount = thisVertCount + otherVertCount;

	progress_title = "verticies";
	progress = 0;
	progress_total = 3;
	print_merge_progress();

	vec3* newVerts = new vec3[totalVertCount];
	memcpy(newVerts, thisVerts, thisVertCount * sizeof(vec3));
	print_merge_progress();
	memcpy(newVerts + thisVertCount, otherVerts, otherVertCount * sizeof(vec3));
	print_merge_progress();

	delete[] mapA.lumps[LUMP_VERTICES];
	mapA.lumps[LUMP_VERTICES] = (byte*)newVerts;
	mapA.header.lump[LUMP_VERTICES].nLength = totalVertCount * sizeof(vec3);
}

void BspMerger::merge_texinfo(Bsp& mapA, Bsp& mapB) {
	BSPTEXTUREINFO* thisInfo = (BSPTEXTUREINFO*)mapA.lumps[LUMP_TEXINFO];
	BSPTEXTUREINFO* otherInfo = (BSPTEXTUREINFO*)mapB.lumps[LUMP_TEXINFO];
	int thisInfoCount = mapA.header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);
	int otherInfoCount = mapB.header.lump[LUMP_TEXINFO].nLength / sizeof(BSPTEXTUREINFO);

	progress_title = "texture info";
	progress = 0;
	progress_total = thisInfoCount + otherInfoCount;

	vector<BSPTEXTUREINFO> mergedInfo;
	mergedInfo.reserve(thisInfoCount + otherInfoCount);

	for (int i = 0; i < thisInfoCount; i++) {
		mergedInfo.push_back(thisInfo[i]);
		print_merge_progress();
	}

	for (int i = 0; i < otherInfoCount; i++) {
		BSPTEXTUREINFO info = otherInfo[i];
		info.iMiptex = texRemap[info.iMiptex];

		bool isUnique = true;
		for (int k = 0; k < thisInfoCount; k++) {
			if (memcmp(&info, &thisInfo[k], sizeof(BSPTEXTUREINFO)) == 0) {
				texInfoRemap.push_back(k);
				isUnique = false;
				break;
			}
		}

		if (isUnique) {
			texInfoRemap.push_back(mergedInfo.size());
			mergedInfo.push_back(info);
		}
		print_merge_progress();
	}

	int newLen = mergedInfo.size() * sizeof(BSPTEXTUREINFO);
	int duplicates = mergedInfo.size() - (thisInfoCount + otherInfoCount);

	if (duplicates) {
		cout << "Removed " << duplicates << " duplicate texinfos\n";
		cout << "ZOMG NOT READY FOR THIS\n";
		// TODO: update plane references in other BSP when duplicates are removed
	}

	delete[] mapA.lumps[LUMP_TEXINFO];
	mapA.lumps[LUMP_TEXINFO] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_TEXINFO], &mergedInfo[0], newLen);
	mapA.header.lump[LUMP_TEXINFO].nLength = newLen;

	//cout << thisInfoCount << " -> " << mergedInfo.size() << endl;
}

void BspMerger::merge_faces(Bsp& mapA, Bsp& mapB) {
	BSPFACE* thisFaces = (BSPFACE*)mapA.lumps[LUMP_FACES];
	BSPFACE* otherFaces = (BSPFACE*)mapB.lumps[LUMP_FACES];
	thisFaceCount = mapA.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int otherFaceCount = mapB.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);
	int totalFaceCount = thisFaceCount + otherFaceCount;

	progress_title = "faces";
	progress = 0;
	progress_total = totalFaceCount + 1;
	print_merge_progress();

	BSPFACE* newFaces = new BSPFACE[totalFaceCount];
	memcpy(newFaces, thisFaces, thisFaceCount * sizeof(BSPFACE));
	memcpy(newFaces + thisFaceCount, otherFaces, otherFaceCount * sizeof(BSPFACE));

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		BSPFACE& face = newFaces[i];
		face.iPlane = planeRemap[face.iPlane];
		face.iFirstEdge = face.iFirstEdge + thisSurfEdgeCount;
		face.iTextureInfo = texInfoRemap[face.iTextureInfo];
		print_merge_progress();
	}

	delete[] mapA.lumps[LUMP_FACES];
	mapA.lumps[LUMP_FACES] = (byte*)newFaces;
	mapA.header.lump[LUMP_FACES].nLength = totalFaceCount * sizeof(BSPFACE);
}

void BspMerger::merge_leaves(Bsp& mapA, Bsp& mapB) {
	BSPLEAF* thisLeaves = (BSPLEAF*)mapA.lumps[LUMP_LEAVES];
	BSPLEAF* otherLeaves = (BSPLEAF*)mapB.lumps[LUMP_LEAVES];
	thisLeafCount = mapA.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);
	otherLeafCount = mapB.header.lump[LUMP_LEAVES].nLength / sizeof(BSPLEAF);

	int thisWorldLeafCount = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])->nVisLeafs + 1; // include solid leaf

	progress_title = "leaves";
	progress = 0;
	progress_total = thisLeafCount + otherLeafCount;

	vector<BSPLEAF> mergedLeaves;
	mergedLeaves.reserve(thisWorldLeafCount + otherLeafCount);
	modelLeafRemap.reserve(thisWorldLeafCount + otherLeafCount);

	for (int i = 0; i < thisWorldLeafCount; i++) {
		modelLeafRemap.push_back(i);
		mergedLeaves.push_back(thisLeaves[i]);
		print_merge_progress();
	}

	for (int i = 0; i < otherLeafCount; i++) {
		BSPLEAF& leaf = otherLeaves[i];
		if (leaf.nMarkSurfaces) {
			leaf.iFirstMarkSurface = leaf.iFirstMarkSurface + thisMarkSurfCount;
		}

		bool isSharedSolidLeaf = i == 0;
		if (!isSharedSolidLeaf) {
			leavesRemap.push_back(mergedLeaves.size());
			mergedLeaves.push_back(leaf);
		}
		else {
			// always exclude the first solid leaf since there can only be one per map, at index 0
			leavesRemap.push_back(0);
		}
		print_merge_progress();
	}

	// append A's submodel leaves after B's world leaves
	// Order will be: A's world leaves -> B's world leaves -> B's submodel leaves -> A's submodel leaves
	for (int i = thisWorldLeafCount; i < thisLeafCount; i++) {
		modelLeafRemap.push_back(mergedLeaves.size());
		mergedLeaves.push_back(thisLeaves[i]);
	}

	otherLeafCount -= 1; // solid leaf removed

	int newLen = mergedLeaves.size() * sizeof(BSPLEAF);

	delete[] mapA.lumps[LUMP_LEAVES];
	mapA.lumps[LUMP_LEAVES] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_LEAVES], &mergedLeaves[0], newLen);
	mapA.header.lump[LUMP_LEAVES].nLength = newLen;

	//cout << thisLeafCount << " -> " << mergedLeaves.size() << endl;
}

void BspMerger::merge_marksurfs(Bsp& mapA, Bsp& mapB) {
	uint16* thisMarks = (uint16*)mapA.lumps[LUMP_MARKSURFACES];
	uint16* otherMarks = (uint16*)mapB.lumps[LUMP_MARKSURFACES];
	thisMarkSurfCount = mapA.header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);
	int otherMarkCount = mapB.header.lump[LUMP_MARKSURFACES].nLength / sizeof(uint16);
	int totalSurfCount = thisMarkSurfCount + otherMarkCount;

	progress_title = "mark surfaces";
	progress = 0;
	progress_total = otherMarkCount + 1;
	print_merge_progress();

	uint16* newSurfs = new uint16[totalSurfCount];
	memcpy(newSurfs, thisMarks, thisMarkSurfCount * sizeof(uint16));
	memcpy(newSurfs + thisMarkSurfCount, otherMarks, otherMarkCount * sizeof(uint16));

	for (int i = thisMarkSurfCount; i < totalSurfCount; i++) {
		uint16& mark = newSurfs[i];
		mark = mark + thisFaceCount;
		print_merge_progress();
	}

	delete[] mapA.lumps[LUMP_MARKSURFACES];
	mapA.lumps[LUMP_MARKSURFACES] = (byte*)newSurfs;
	mapA.header.lump[LUMP_MARKSURFACES].nLength = totalSurfCount * sizeof(uint16);
}

void BspMerger::merge_edges(Bsp& mapA, Bsp& mapB) {
	BSPEDGE* thisEdges = (BSPEDGE*)mapA.lumps[LUMP_EDGES];
	BSPEDGE* otherEdges = (BSPEDGE*)mapB.lumps[LUMP_EDGES];
	thisEdgeCount = mapA.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int otherEdgeCount = mapB.header.lump[LUMP_EDGES].nLength / sizeof(BSPEDGE);
	int totalEdgeCount = thisEdgeCount + otherEdgeCount;

	progress_title = "edges";
	progress = 0;
	progress_total = otherEdgeCount + 1;
	print_merge_progress();

	BSPEDGE* newEdges = new BSPEDGE[totalEdgeCount];
	memcpy(newEdges, thisEdges, thisEdgeCount * sizeof(BSPEDGE));
	memcpy(newEdges + thisEdgeCount, otherEdges, otherEdgeCount * sizeof(BSPEDGE));

	for (int i = thisEdgeCount; i < totalEdgeCount; i++) {
		BSPEDGE& edge = newEdges[i];
		edge.iVertex[0] = edge.iVertex[0] + thisVertCount;
		edge.iVertex[1] = edge.iVertex[1] + thisVertCount;
		print_merge_progress();
	}

	delete[] mapA.lumps[LUMP_EDGES];
	mapA.lumps[LUMP_EDGES] = (byte*)newEdges;
	mapA.header.lump[LUMP_EDGES].nLength = totalEdgeCount * sizeof(BSPEDGE);
}

void BspMerger::merge_surfedges(Bsp& mapA, Bsp& mapB) {
	int32_t* thisSurfs = (int32_t*)mapA.lumps[LUMP_SURFEDGES];
	int32_t* otherSurfs = (int32_t*)mapB.lumps[LUMP_SURFEDGES];
	thisSurfEdgeCount = mapA.header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int otherSurfCount = mapB.header.lump[LUMP_SURFEDGES].nLength / sizeof(int32_t);
	int totalSurfCount = thisSurfEdgeCount + otherSurfCount;

	progress_title = "surface edges";
	progress = 0;
	progress_total = otherSurfCount + 1;
	print_merge_progress();

	int32_t* newSurfs = new int32_t[totalSurfCount];
	memcpy(newSurfs, thisSurfs, thisSurfEdgeCount * sizeof(int32_t));
	memcpy(newSurfs + thisSurfEdgeCount, otherSurfs, otherSurfCount * sizeof(int32_t));

	for (int i = thisSurfEdgeCount; i < totalSurfCount; i++) {
		int32_t& surfEdge = newSurfs[i];
		surfEdge = surfEdge < 0 ? surfEdge - thisEdgeCount : surfEdge + thisEdgeCount;
		print_merge_progress();
	}

	delete[] mapA.lumps[LUMP_SURFEDGES];
	mapA.lumps[LUMP_SURFEDGES] = (byte*)newSurfs;
	mapA.header.lump[LUMP_SURFEDGES].nLength = totalSurfCount * sizeof(int32_t);
}

void BspMerger::merge_nodes(Bsp& mapA, Bsp& mapB) {
	BSPNODE* thisNodes = (BSPNODE*)mapA.lumps[LUMP_NODES];
	BSPNODE* otherNodes = (BSPNODE*)mapB.lumps[LUMP_NODES];
	thisNodeCount = mapA.header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);
	int otherNodeCount = mapB.header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);

	progress_title = "nodes";
	progress = 0;
	progress_total = thisNodeCount + otherNodeCount;

	vector<BSPNODE> mergedNodes;
	mergedNodes.reserve(thisNodeCount + otherNodeCount);

	for (int i = 0; i < thisNodeCount; i++) {
		BSPNODE node = thisNodes[i];

		if (i > 0) { // new headnode should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += 1; // shifted from new head node
				}
				else {
					node.iChildren[k] = ~((int16_t)modelLeafRemap[~node.iChildren[k]]);
				}
			}
		}

		mergedNodes.push_back(node);
		print_merge_progress();
	}

	for (int i = 0; i < otherNodeCount; i++) {
		BSPNODE node = otherNodes[i];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisNodeCount;
			}
			else {
				node.iChildren[k] = ~((int16_t)leavesRemap[~node.iChildren[k]]);
			}
		}
		node.iPlane = planeRemap[node.iPlane];
		if (node.nFaces) {
			node.firstFace = node.firstFace + thisFaceCount;
		}

		mergedNodes.push_back(node);
		print_merge_progress();
	}

	int newLen = mergedNodes.size() * sizeof(BSPNODE);

	delete[] mapA.lumps[LUMP_NODES];
	mapA.lumps[LUMP_NODES] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_NODES], &mergedNodes[0], newLen);
	mapA.header.lump[LUMP_NODES].nLength = newLen;

	//cout << thisNodeCount << " -> " << mergedNodes.size() << endl;
}

void BspMerger::merge_clipnodes(Bsp& mapA, Bsp& mapB) {
	BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)mapA.lumps[LUMP_CLIPNODES];
	BSPCLIPNODE* otherNodes = (BSPCLIPNODE*)mapB.lumps[LUMP_CLIPNODES];
	thisClipnodeCount = mapA.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
	int otherClipnodeCount = mapB.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);

	progress_title = "clipnodes";
	progress = 0;
	progress_total = thisClipnodeCount + otherClipnodeCount;

	vector<BSPCLIPNODE> mergedNodes;
	mergedNodes.reserve(thisClipnodeCount + otherClipnodeCount);

	for (int i = 0; i < thisClipnodeCount; i++) {
		BSPCLIPNODE node = thisNodes[i];
		if (i > 2) { // new headnodes should already be correct
			for (int k = 0; k < 2; k++) {
				if (node.iChildren[k] >= 0) {
					node.iChildren[k] += MAX_MAP_HULLS - 1; // offset from new headnodes being added
				}
			}
		}
		mergedNodes.push_back(node);
		print_merge_progress();
	}

	for (int i = 0; i < otherClipnodeCount; i++) {
		BSPCLIPNODE node = otherNodes[i];
		node.iPlane = planeRemap[node.iPlane];

		for (int k = 0; k < 2; k++) {
			if (node.iChildren[k] >= 0) {
				node.iChildren[k] += thisClipnodeCount;
			}
		}
		mergedNodes.push_back(node);
		print_merge_progress();
	}

	int newLen = mergedNodes.size() * sizeof(BSPCLIPNODE);

	delete[] mapA.lumps[LUMP_CLIPNODES];
	mapA.lumps[LUMP_CLIPNODES] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_CLIPNODES], &mergedNodes[0], newLen);
	mapA.header.lump[LUMP_CLIPNODES].nLength = newLen;

	//cout << thisClipnodeCount << " -> " << mergedNodes.size() << endl;
}

void BspMerger::merge_models(Bsp& mapA, Bsp& mapB) {
	BSPMODEL* thisModels = (BSPMODEL*)mapA.lumps[LUMP_MODELS];
	BSPMODEL* otherModels = (BSPMODEL*)mapB.lumps[LUMP_MODELS];
	int thisModelCount = mapA.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);
	int otherModelCount = mapB.header.lump[LUMP_MODELS].nLength / sizeof(BSPMODEL);

	progress_title = "models";
	progress = 0;
	progress_total = thisModelCount + otherModelCount;

	vector<BSPMODEL> mergedModels;
	mergedModels.reserve(thisModelCount + otherModelCount);

	// merged world model
	mergedModels.push_back(thisModels[0]);

	// other map's submodels
	for (int i = 1; i < otherModelCount; i++) {
		BSPMODEL model = otherModels[i];
		model.iHeadnodes[0] += thisNodeCount + 1;
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			model.iHeadnodes[k] += thisClipnodeCount;
		}
		model.iFirstFace = model.iFirstFace + thisFaceCount;
		mergedModels.push_back(model);
		print_merge_progress();
	}

	// this map's submodels
	for (int i = 1; i < thisModelCount; i++) {
		BSPMODEL model = thisModels[i];
		model.iHeadnodes[0] += 1; // adjust for new head node
		for (int k = 1; k < MAX_MAP_HULLS; k++) {
			model.iHeadnodes[k] += (MAX_MAP_HULLS - 1); // adjust for new head nodes
		}
		mergedModels.push_back(model);
		print_merge_progress();
	}

	// update world head nodes
	mergedModels[0].iHeadnodes[0] = 0;
	mergedModels[0].iHeadnodes[1] = 0;
	mergedModels[0].iHeadnodes[2] = 1;
	mergedModels[0].iHeadnodes[3] = 2;
	mergedModels[0].nVisLeafs = thisModels[0].nVisLeafs + otherModels[0].nVisLeafs;
	mergedModels[0].nFaces = thisModels[0].nFaces + otherModels[0].nFaces;

	vec3 amin = thisModels[0].nMins;
	vec3 bmin = otherModels[0].nMins;
	vec3 amax = thisModels[0].nMaxs;
	vec3 bmax = otherModels[0].nMaxs;
	mergedModels[0].nMins = { min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) };
	mergedModels[0].nMaxs = { max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) };

	int newLen = mergedModels.size() * sizeof(BSPMODEL);

	delete[] mapA.lumps[LUMP_MODELS];
	mapA.lumps[LUMP_MODELS] = new byte[newLen];
	memcpy(mapA.lumps[LUMP_MODELS], &mergedModels[0], newLen);
	mapA.header.lump[LUMP_MODELS].nLength = newLen;

	//cout << thisModelCount << " -> " << mergedModels.size() << endl;
}

void BspMerger::merge_vis(Bsp& mapA, Bsp& mapB) {
	byte* thisVis = (byte*)mapA.lumps[LUMP_VISIBILITY];
	byte* otherVis = (byte*)mapB.lumps[LUMP_VISIBILITY];

	BSPLEAF* allLeaves = (BSPLEAF*)mapA.lumps[LUMP_LEAVES];

	int thisVisLeaves = thisLeafCount - 1; // VIS ignores the shared solid leaf 0
	int otherVisLeaves = otherLeafCount; // already does not include the solid leaf (see merge_leaves)
	int totalVisLeaves = thisVisLeaves + otherVisLeaves;

	int thisModelLeafCount = thisVisLeaves - thisWorldLeafCount;
	int otherModelLeafCount = otherVisLeaves - otherWorldLeafCount;

	uint newVisRowSize = ((totalVisLeaves + 63) & ~63) >> 3;
	int decompressedVisSize = totalVisLeaves * newVisRowSize;

	// submodel leaves should come after world leaves and need to be moved after the incoming world leaves from the other map
	int shiftOffsetBit = 0; // where to start making room for the submodel leaves
	int shiftAmount = otherLeafCount;
	for (int k = 0; k < modelLeafRemap.size(); k++) {
		if (k != modelLeafRemap[k]) {
			shiftOffsetBit = k - 1; // skip solid leaf
			break;
		}
	}

	progress_title = "visibility";
	progress = 0;
	progress_total = thisWorldLeafCount + thisModelLeafCount + otherLeafCount;

	byte* decompressedVis = new byte[decompressedVisSize];
	memset(decompressedVis, 0, decompressedVisSize);

	// decompress this map's world leaves
	decompress_vis_lump(allLeaves, thisVis, decompressedVis,
		thisWorldLeafCount, thisVisLeaves, totalVisLeaves,
		shiftOffsetBit, shiftAmount);

	// decompress this map's model leaves (also making room for the other map's world leaves)
	BSPLEAF* thisModelLeaves = allLeaves + thisWorldLeafCount + otherLeafCount;
	byte* modelLeafVisDest = decompressedVis + (thisWorldLeafCount + otherLeafCount) * newVisRowSize;
	decompress_vis_lump(thisModelLeaves, thisVis, modelLeafVisDest,
		thisModelLeafCount, thisVisLeaves, totalVisLeaves,
		shiftOffsetBit, shiftAmount);

	//cout << "Decompressed this vis:\n";
	//print_vis(decompressedVis, thisVisLeaves + otherLeafCount, newVisRowSize);

	// all of other map's leaves come after this map's world leaves
	shiftOffsetBit = 0;
	shiftAmount = thisWorldLeafCount; // world leaf count (exluding solid leaf)

	// decompress other map's vis data (skip empty first leaf, which now only the first map should have)
	byte* decompressedOtherVis = decompressedVis + thisWorldLeafCount * newVisRowSize;
	decompress_vis_lump(allLeaves + thisWorldLeafCount, otherVis, decompressedOtherVis,
		otherLeafCount, otherLeafCount, totalVisLeaves,
		shiftOffsetBit, shiftAmount);

	//cout << "Decompressed other vis:\n";
	//print_vis(decompressedOtherVis, otherLeafCount, newVisRowSize);

	//memset(decompressedVis + 9 * newBitbytes, 0xff, otherMapVisSize);

	//cout << "Decompressed combined vis:\n";
	//print_vis(decompressedVis, totalVisLeaves, newVisRowSize);

	// recompress the combined vis data
	byte* compressedVis = new byte[decompressedVisSize];
	memset(compressedVis, 0, decompressedVisSize);
	int newVisLen = CompressAll(allLeaves, decompressedVis, compressedVis, totalVisLeaves);
	int oldLen = mapA.header.lump[LUMP_VISIBILITY].nLength;

	delete[] mapA.lumps[LUMP_VISIBILITY];
	mapA.lumps[LUMP_VISIBILITY] = new byte[newVisLen];
	memcpy(mapA.lumps[LUMP_VISIBILITY], compressedVis, newVisLen);
	mapA.header.lump[LUMP_VISIBILITY].nLength = newVisLen;

	//cout << oldLen << " -> " << newVisLen << endl;
}

void BspMerger::merge_lighting(Bsp& mapA, Bsp& mapB) {
	BSPFACE* faces = (BSPFACE*)mapA.lumps[LUMP_FACES];
	COLOR3* thisRad = (COLOR3*)mapA.lumps[LUMP_LIGHTING];
	COLOR3* otherRad = (COLOR3*)mapB.lumps[LUMP_LIGHTING];
	int thisColorCount = mapA.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int otherColorCount = mapB.header.lump[LUMP_LIGHTING].nLength / sizeof(COLOR3);
	int totalColorCount = thisColorCount + otherColorCount;
	int totalFaceCount = mapA.header.lump[LUMP_FACES].nLength / sizeof(BSPFACE);

	progress_title = "lightmaps";
	progress = 0;
	progress_total = 4 + totalFaceCount;

	// create a single full-bright lightmap to use for all faces, if one map has lighting but the other doesn't
	if (thisColorCount == 0 && otherColorCount != 0) {
		thisColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += thisColorCount;
		int sz = thisColorCount * sizeof(COLOR3);
		mapA.lumps[LUMP_LIGHTING] = new byte[sz];
		mapA.header.lump[LUMP_LIGHTING].nLength = sz;
		thisRad = (COLOR3*)mapA.lumps[LUMP_LIGHTING];

		memset(thisRad, 255, sz);

		for (int i = 0; i < thisFaceCount; i++) {
			faces[i].nLightmapOffset = 0;
		}
	}
	else if (thisColorCount != 0 && otherColorCount == 0) {
		otherColorCount = MAX_SURFACE_EXTENT * MAX_SURFACE_EXTENT;
		totalColorCount += otherColorCount;
		otherRad = new COLOR3[otherColorCount];

		memset(otherRad, 255, otherColorCount * sizeof(COLOR3));

		for (int i = thisFaceCount; i < totalFaceCount; i++) {
			faces[i].nLightmapOffset = 0;
		}
	}

	COLOR3* newRad = new COLOR3[totalColorCount];
	print_merge_progress();

	memcpy(newRad, thisRad, thisColorCount * sizeof(COLOR3));
	print_merge_progress();

	memcpy((byte*)newRad + thisColorCount * sizeof(COLOR3), otherRad, otherColorCount * sizeof(COLOR3));
	print_merge_progress();


	delete[] mapA.lumps[LUMP_LIGHTING];
	mapA.lumps[LUMP_LIGHTING] = (byte*)newRad;
	int oldLen = mapA.header.lump[LUMP_LIGHTING].nLength;
	mapA.header.lump[LUMP_LIGHTING].nLength = totalColorCount * sizeof(COLOR3);
	print_merge_progress();

	for (int i = thisFaceCount; i < totalFaceCount; i++) {
		faces[i].nLightmapOffset += thisColorCount * sizeof(COLOR3);
		print_merge_progress();
	}

	//cout << oldLen << " -> " << header.lump[LUMP_LIGHTING].nLength << endl;
}

bool BspMerger::shiftVis(uint64* vis, int len, int offsetLeaf, int shift) {
	byte bitsPerStep = 64;
	byte offsetBit = offsetLeaf % bitsPerStep;
	uint64 mask = 0; // part of the byte that shouldn't be shifted
	for (int i = 0; i < offsetBit; i++) {
		mask |= 1 << i;
	}

	len /= 8; // byte -> uint64 (vis rows are always divisible by 8)

	int overflow = 0;
	for (int k = 0; k < shift; k++) {

		bool carry = 0;
		for (int i = 0; i < len; i++) {
			uint64 oldCarry = carry;
			carry = (vis[i] & 0x8000000000000000L) != 0;

			if (offsetBit != 0 && i * bitsPerStep < offsetLeaf && i * bitsPerStep + bitsPerStep > offsetLeaf) {
				vis[i] = (vis[i] & mask) | ((vis[i] & ~mask) << 1);
			}
			else if (i >= offsetLeaf / bitsPerStep) {
				vis[i] = (vis[i] << 1) + oldCarry;
			}
			else {
				carry = 0;
			}
		}

		if (carry) {
			overflow++;
		}
	}
	if (overflow)
		printf("OVERFLOWED %d VIS LEAVES WHILE SHIFTING\n", overflow);

	return overflow;
}

// decompress this map's vis data into arrays of bits where each bit indicates if a leaf is visible or not
// iterationLeaves = number of leaves to decompress vis for
// visDataLeafCount = total leaves in this map (exluding the shared solid leaf 0)
// newNumLeaves = total leaves that will be in the map after merging is finished (again, excluding solid leaf 0)
void BspMerger::decompress_vis_lump(BSPLEAF* leafLump, byte* visLump, byte* output,
	int iterationLeaves, int visDataLeafCount, int newNumLeaves,
	int shiftOffsetBit, int shiftAmount)
{
	byte* dest;
	uint oldVisRowSize = ((visDataLeafCount + 63) & ~63) >> 3;
	uint newVisRowSize = ((newNumLeaves + 63) & ~63) >> 3;
	int len = 0;

	// calculate which bits of an uncompressed visibility row are used/unused
	uint64 lastChunkMask = 0;
	int lastChunkIdx = (oldVisRowSize / 8) - 1;
	int maxBitsInLastChunk = (visDataLeafCount % 64);
	for (uint64 k = 0; k < maxBitsInLastChunk; k++) {
		lastChunkMask = lastChunkMask | ((uint64)1 << k);
	}

	for (int i = 0; i < iterationLeaves; i++)
	{
		dest = output + i * newVisRowSize;

		if (leafLump[i + 1].nVisOffset < 0) {
			memset(dest, 255, visDataLeafCount / 8);
			for (int k = 0; k < visDataLeafCount % 8; k++) {
				dest[visDataLeafCount / 8] |= 1 << k;
			}
			shiftVis((uint64*)dest, newVisRowSize, shiftOffsetBit, shiftAmount);
			continue;
		}

		DecompressVis((const byte*)(visLump + leafLump[i + 1].nVisOffset), dest, oldVisRowSize, visDataLeafCount);

		// Leaf visibility row lengths are multiples of 64 leaves, so there are usually some unused bits at the end.
		// Maps sometimes set those unused bits randomly (e.g. leaf index 100 is marked visible, but there are only 90 leaves...)
		// To prevent overflows when shifting the data later, the unused leaf bits will be forced to zero here.
		((uint64*)dest)[lastChunkIdx] &= lastChunkMask;

		if (shiftAmount) {
			shiftVis((uint64*)dest, newVisRowSize, shiftOffsetBit, shiftAmount);
		}

		print_merge_progress();
	}
}


void BspMerger::create_merge_headnodes(Bsp& mapA, Bsp& mapB, BSPPLANE separationPlane) {
	BSPMODEL& thisWorld = ((BSPMODEL*)mapA.lumps[LUMP_MODELS])[0];
	BSPMODEL& otherWorld = ((BSPMODEL*)mapB.lumps[LUMP_MODELS])[0];

	vec3 amin = thisWorld.nMins;
	vec3 amax = thisWorld.nMaxs;
	vec3 bmin = otherWorld.nMins;
	vec3 bmax = otherWorld.nMaxs;

	// planes with negative normals mess up VIS and lighting stuff, so swap children instead
	bool swapNodeChildren = separationPlane.vNormal.x < 0 || separationPlane.vNormal.y < 0 || separationPlane.vNormal.z < 0;
	if (swapNodeChildren)
		separationPlane.vNormal = separationPlane.vNormal.invert();

	//printf("Separating plane: (%.0f, %.0f, %.0f) %.0f\n", separationPlane.vNormal.x, separationPlane.vNormal.y, separationPlane.vNormal.z, separationPlane.fDist);

	// write separating plane
	BSPPLANE* thisPlanes = (BSPPLANE*)mapA.lumps[LUMP_PLANES];
	int numThisPlanes = mapA.header.lump[LUMP_PLANES].nLength / sizeof(BSPPLANE);

	BSPPLANE* newThisPlanes = new BSPPLANE[numThisPlanes + 1];
	memcpy(newThisPlanes, thisPlanes, numThisPlanes * sizeof(BSPPLANE));
	newThisPlanes[numThisPlanes] = separationPlane;

	delete[] mapA.lumps[LUMP_PLANES];
	mapA.lumps[LUMP_PLANES] = (byte*)newThisPlanes;
	mapA.header.lump[LUMP_PLANES].nLength = (numThisPlanes + 1) * sizeof(BSPPLANE);

	int separationPlaneIdx = numThisPlanes;


	// write new head node (visible BSP)
	{
		BSPNODE* thisNodes = (BSPNODE*)mapA.lumps[LUMP_NODES];
		int numThisNodes = mapA.header.lump[LUMP_NODES].nLength / sizeof(BSPNODE);

		BSPNODE headNode = {
			separationPlaneIdx,			// plane idx
			{numThisNodes + 1, 1},		// child nodes
			{ min(amin.x, bmin.x), min(amin.y, bmin.y), min(amin.z, bmin.z) },	// mins
			{ max(amax.x, bmax.x), max(amax.y, bmax.y), max(amax.z, bmax.z) },	// maxs
			0, // first face
			0  // n faces (none since this plane is in the void)
		};

		if (swapNodeChildren) {
			int16_t temp = headNode.iChildren[0];
			headNode.iChildren[0] = headNode.iChildren[1];
			headNode.iChildren[1] = temp;
		}

		BSPNODE* newThisNodes = new BSPNODE[numThisNodes + 1];
		memcpy(newThisNodes + 1, thisNodes, numThisNodes * sizeof(BSPNODE));
		newThisNodes[0] = headNode;

		delete[] mapA.lumps[LUMP_NODES];
		mapA.lumps[LUMP_NODES] = (byte*)newThisNodes;
		mapA.header.lump[LUMP_NODES].nLength = (numThisNodes + 1) * sizeof(BSPNODE);
	}


	// write new head node (clipnode BSP)
	{
		BSPCLIPNODE* thisNodes = (BSPCLIPNODE*)mapA.lumps[LUMP_CLIPNODES];
		int numThisNodes = mapA.header.lump[LUMP_CLIPNODES].nLength / sizeof(BSPCLIPNODE);
		const int NEW_NODE_COUNT = MAX_MAP_HULLS - 1;

		BSPCLIPNODE newHeadNodes[NEW_NODE_COUNT];
		for (int i = 0; i < NEW_NODE_COUNT; i++) {
			//printf("HULL %d starts at %d\n", i+1, thisWorld.iHeadnodes[i+1]);
			newHeadNodes[i] = {
				separationPlaneIdx,	// plane idx
				{	// child nodes
					(int16_t)(otherWorld.iHeadnodes[i + 1] + numThisNodes + NEW_NODE_COUNT),
					(int16_t)(thisWorld.iHeadnodes[i + 1] + NEW_NODE_COUNT)
				},
			};

			if (swapNodeChildren) {
				int16_t temp = newHeadNodes[i].iChildren[0];
				newHeadNodes[i].iChildren[0] = newHeadNodes[i].iChildren[1];
				newHeadNodes[i].iChildren[1] = temp;
			}
		}

		BSPCLIPNODE* newThisNodes = new BSPCLIPNODE[numThisNodes + NEW_NODE_COUNT];
		memcpy(newThisNodes, newHeadNodes, NEW_NODE_COUNT * sizeof(BSPCLIPNODE));
		memcpy(newThisNodes + NEW_NODE_COUNT, thisNodes, numThisNodes * sizeof(BSPCLIPNODE));

		delete[] mapA.lumps[LUMP_CLIPNODES];
		mapA.lumps[LUMP_CLIPNODES] = (byte*)newThisNodes;
		mapA.header.lump[LUMP_CLIPNODES].nLength = (numThisNodes + NEW_NODE_COUNT) * sizeof(BSPCLIPNODE);
	}
}

void BspMerger::print_merge_progress() {
	if (progress++ > 0) {
		auto now = std::chrono::system_clock::now();
		std::chrono::duration<double> delta = now - last_progress;
		if (delta.count() < 0.016) {
			return;
		}
		last_progress = now;
	}

	int percent = (progress / (float)progress_total) * 100;

	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b");
	printf("    Merging %-13s %2d%%", progress_title, percent);
}

