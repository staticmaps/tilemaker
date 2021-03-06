#ifndef _OSM_OBJECT_H
#define _OSM_OBJECT_H

#include <vector>
#include <string>
#include <sstream>
#include <map>
#include "kaguya.hpp"
#include "geomtypes.h"
#include "osm_store.h"
#include "output_object.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// Protobuf
#include "osmformat.pb.h"
#include "vector_tile.pb.h"

struct LayerDef {
	std::string name;
	uint minzoom;
	uint maxzoom;
	uint simplifyBelow;
	double simplifyLevel;
	double simplifyLength;
	double simplifyRatio;
	std::map<std::string, uint> attributeMap;
};

/*
	OSMObject - represents the object (from the .osm.pbf) currently being processed
	
	Only one instance of this class is ever used. Its main purpose is to provide a 
	consistent object for Lua to access.
	
*/

class OSMObject { 

public:

	kaguya::State *luaState;				// Lua reference
	std::map<std::string, RTree> *indices;			// Spatial indices
	std::vector<Geometry> *cachedGeometries;		// Cached geometries
	std::map<uint,std::string> *cachedGeometryNames;	// Cached geometry names
	OSMStore *osmStore;						// Global OSM store

	uint64_t osmID;							// ID of OSM object
	WayID newWayID = MAX_WAY_ID;			// Decrementing new ID for relations
	bool isWay, isRelation;					// Way, node, relation?

	int32_t lon1,latp1,lon2,latp2;			// Start/end co-ordinates of OSM object
	NodeVec *nodeVec;						// node vector
	WayVec *outerWayVec, *innerWayVec;		// way vectors

	Linestring linestringCache;
	bool linestringInited;
	Polygon polygonCache;
	bool polygonInited;
	MultiPolygon multiPolygonCache;
	bool multiPolygonInited;

	std::vector<LayerDef> layers;				// List of layers
	std::map<std::string,uint> layerMap;				// Layer->position map
	std::vector<std::vector<uint> > layerOrder;		// Order of (grouped) layers, e.g. [ [0], [1,2,3], [4] ]

	std::vector<OutputObject> outputs;			// All output objects

	// Common tag storage
	std::vector<std::string> stringTable;				// Tag table from the current PrimitiveGroup
	std::map<std::string, uint> tagMap;				// String->position map

	// Tag storage for denseNodes
	uint denseStart;							// Start of key/value table section (DenseNodes)
	uint denseEnd;							// End of key/value table section (DenseNodes)
	DenseNodes *densePtr;					// DenseNodes object

	// Tag storage for ways/relations
	::google::protobuf::RepeatedField< ::google::protobuf::uint32 > *keysPtr;
	::google::protobuf::RepeatedField< ::google::protobuf::uint32 > *valsPtr;
	uint tagLength;

	// ----	initialization routines

	OSMObject(kaguya::State *luaPtr, std::map< std::string, RTree> *idxPtr, std::vector<Geometry> *geomPtr, std::map<uint,std::string> *namePtr, OSMStore *storePtr);

	// Define a layer (as read from the .json file)
	uint addLayer(std::string name, uint minzoom, uint maxzoom,
			uint simplifyBelow, double simplifyLevel, double simplifyLength, double simplifyRatio, std::string writeTo);

	// Read string dictionary from the .pbf
	void readStringTable(PrimitiveBlock *pbPtr);

	// ----	Helpers provided for main routine

	// Has this object been assigned to any layers?
	bool empty();

	// Find a string in the dictionary
	int findStringPosition(std::string str);

	// ----	Set an osm element to make it accessible from Lua

	// We are now processing a node
	inline void setNode(NodeID id, DenseNodes *dPtr, int kvStart, int kvEnd, LatpLon node) {
		reset();
		osmID = id;
		isWay = false;
		isRelation = false;

		setLocation(node.lon, node.latp, node.lon, node.latp);

		denseStart = kvStart;
		denseEnd = kvEnd;
		densePtr = dPtr;
	}

	// We are now processing a way
	inline void setWay(Way *way, NodeVec *nodeVecPtr) {
		reset();
		osmID = way->id();
		isWay = true;
		isRelation = false;

		nodeVec = nodeVecPtr;
		try
		{
			setLocation(osmStore->nodes.at(nodeVec->front()).lon, osmStore->nodes.at(nodeVec->front()).latp,
					osmStore->nodes.at(nodeVec->back()).lon, osmStore->nodes.at(nodeVec->back()).latp);
		}
		catch (std::out_of_range &err)
		{
			std::stringstream ss;
			ss << "Way " << osmID << " is missing a node";
			throw std::out_of_range(ss.str());
		}

		keysPtr = way->mutable_keys();
		valsPtr = way->mutable_vals();
		tagLength = way->keys_size();
	}

	// We are now processing a relation
	// (note that we store relations as ways with artificial IDs, and that
	//  we use decrementing positive IDs to give a bit more space for way IDs)
	inline void setRelation(Relation *relation, WayVec *outerWayVecPtr, WayVec *innerWayVecPtr) {
		reset();
		osmID = --newWayID;
		isWay = true;
		isRelation = true;

		outerWayVec = outerWayVecPtr;
		innerWayVec = innerWayVecPtr;
		//setLocation(...); TODO

		keysPtr = relation->mutable_keys();
		valsPtr = relation->mutable_vals();
		tagLength = relation->keys_size();
	}

	// Internal: clear current cached state
	inline void reset() {
		outputs.clear();
		linestringInited = false;
		polygonInited = false;
		multiPolygonInited = false;
	}

	// Internal: set start/end co-ordinates
	inline void setLocation(int32_t a, int32_t b, int32_t c, int32_t d) {
		lon1=a; latp1=b; lon2=c; latp2=d;
	}

	// ----	Metadata queries called from Lua

	// Get the ID of the current object
	std::string Id() const;

	// Check if there's a value for a given key
	bool Holds(const std::string& key) const;

	// Get an OSM tag for a given key (or return empty string if none)
	std::string Find(const std::string& key) const;

	// ----	Spatial queries called from Lua

	// Find intersecting shapefile layer
	// TODO: multipolygon relations not supported, will always return false
	std::vector<std::string> FindIntersecting(const std::string &layerName);
	bool Intersects(const std::string &layerName);
	std::vector<uint> findIntersectingGeometries(const std::string &layerName);
	std::vector<uint> verifyIntersectResults(std::vector<IndexValue> &results, Point &p1, Point &p2);
	std::vector<std::string> namesOfGeometries(std::vector<uint> &ids);

	// Returns whether it is closed polygon
	bool IsClosed() const;

	// Scale to (kilo)meter
	double ScaleToMeter();

	double ScaleToKiloMeter();

	// Returns area
	double Area();

	// Returns length
	double Length();

	// Lazy geometries creation
	const Linestring &linestring();

	const Polygon &polygon();

	const MultiPolygon &multiPolygon();

	// ----	Requests from Lua to write this way/node to a vector tile's Layer

	// Add layer
	void Layer(const std::string &layerName, bool area);
	void LayerAsCentroid(const std::string &layerName);
	
	// Set attributes in a vector tile's Attributes table
	void Attribute(const std::string &key, const std::string &val);
	void AttributeNumeric(const std::string &key, const float val);
	void AttributeBoolean(const std::string &key, const bool val);

	// ----	vector_layers metadata entry

	void setVectorLayerMetadata(const uint_least8_t layer, const std::string &key, const uint type);
	std::string serialiseLayerJSON();
};

#endif //_OSM_OBJECT_H
