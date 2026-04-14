#pragma once
// db_source.h — Load skill/behavior data from CDClient SQLite or FDB.
//
// Supports two data sources:
//   1. SQLite (.sqlite) — DarkflameServer's CDServer.sqlite or community CDClient
//   2. FDB (.fdb) — Original client CDClient.fdb (via lu_assets FDB parser)
//
// Both are read into the same in-memory model: skills, behaviors, parameters, template names.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace skill_editor {

// A behavior template type (e.g. "BasicAttack", "And", "AoE")
struct BehaviorTemplateName {
    int template_id = 0;
    std::string name;
};

// A behavior node in the tree
struct BehaviorTemplate {
    int behavior_id = 0;
    int template_id = 0;     // → BehaviorTemplateName
    int effect_id = 0;
    std::string effect_handle;
};

// A parameter on a behavior node
struct BehaviorParameter {
    int behavior_id = 0;
    std::string parameter_id; // e.g. "on_success", "max damage", "radius"
    double value = 0.0;
};

// A skill entry (root of a behavior tree)
struct SkillBehavior {
    int skill_id = 0;
    int behavior_id = 0;      // Root behavior node
    int imagination_cost = 0;
    double cooldown = 0.0;
    int cooldown_group = 0;
    int skill_icon = 0;
    std::string gate_version;
};

// Object-skill assignment
struct ObjectSkill {
    int object_template = 0; // LOT
    int skill_id = 0;
    int cast_on_type = 0;
};

// All loaded data
struct CdClientData {
    std::unordered_map<int, BehaviorTemplateName> template_names;  // templateID → name
    std::unordered_map<int, BehaviorTemplate> behaviors;           // behaviorID → template
    std::unordered_map<int, std::vector<BehaviorParameter>> params; // behaviorID → params
    std::vector<SkillBehavior> skills;
    std::unordered_map<int, std::string> skill_names;              // skillID → display name from locale.xml
    std::unordered_map<int, std::vector<ObjectSkill>> skill_objects; // skillID → objects that use it
    std::unordered_map<int, std::string> object_names;             // LOT → display name from Objects table

    // Helpers
    std::string behavior_type_name(int behavior_id) const;
    std::vector<std::pair<std::string, int>> child_behaviors(int behavior_id) const;

    // How many skills reference this behavior (directly as root)?
    int skill_ref_count(int behavior_id) const;
    // How many other behaviors reference this one (via parameters)?
    int behavior_ref_count(int behavior_id) const;
};

// Load from SQLite file
CdClientData load_from_sqlite(const std::string& path);

// Load from FDB file (uses lu_assets parser)
CdClientData load_from_fdb(const std::string& path);

// Auto-detect format and load
CdClientData load_cdclient(const std::string& path);

// Returns true if a parameter name refers to another behavior ID (forward child).
// Based on DarkflameServer GetAction() calls and known "behavior N" patterns.
bool is_behavior_ref_param(const std::string& param_name);

struct ParamDef {
    std::string name;
    std::string type;   // "int", "float", "bool", "behavior"
};

// A single member of a dynamic parameter group.
struct DynamicParamMember {
    std::string prefix;  // e.g. "behavior ", "include_faction"
    std::string type;    // "int", "float", "bool", "behavior"
};

// A group of repeatable parameters that share the same numbering.
// e.g. SwitchMultiple has "behavior N" + "value N" as a paired group.
// Single (unpaired) dynamic params are a group of one.
struct DynamicParamDef {
    std::vector<DynamicParamMember> members;
};

// Get the known parameter schema for a behavior template name.
// Returns empty vector if unknown.
const std::vector<ParamDef>& get_behavior_param_schema(const std::string& template_name);

// Get the dynamic (repeatable) parameter definitions for a behavior template.
// Returns empty vector if the behavior has no dynamic params.
const std::vector<DynamicParamDef>& get_behavior_dynamic_params(const std::string& template_name);

// Returns true if a parameter is a back-reference (points to an ancestor/sibling,
// not a forward child). These should be drawn as special edges, not tree branches.
// Examples: End's "start_action", AlterChainDelay's "chain_action".
bool is_back_ref_param(const std::string& param_name, int template_id);

// Load skill names from locale.xml (SkillBehavior_<id>_name entries).
// Returns map of skillID → en_US display name.
std::unordered_map<int, std::string> load_locale_skill_names(const std::string& locale_xml_path);

// Write a parameter value back to the database.
void save_parameter(CdClientData& data, const std::string& db_path,
                    int behavior_id, const std::string& parameter_id, double value);

// Update effect_id and effect_handle on a behavior node (BehaviorTemplate table).
void save_effect(CdClientData& data, const std::string& db_path,
                 int behavior_id, int effect_id, const std::string& effect_handle);

// Create a new empty behavior with the given template type. Returns new behaviorID.
int create_behavior(CdClientData& data, const std::string& db_path, int template_id);

// Create a new skill pointing to an existing behavior. Returns new skillID.
int create_skill(CdClientData& data, const std::string& db_path, int root_behavior_id,
                 int imagination_cost = 0, double cooldown = 0.0);

// Deep-clone a behavior subtree: copies the behavior and all its child behaviors
// recursively, assigning new IDs. Returns the new root behaviorID.
int clone_subtree(CdClientData& data, const std::string& db_path, int source_behavior_id);

// Clone an entire skill (clones the root behavior subtree + creates new skill entry).
// Returns new skillID.
int clone_skill(CdClientData& data, const std::string& db_path, int source_skill_id);

// Delete a behavior and all its parameters from the database.
void delete_behavior(CdClientData& data, const std::string& db_path, int behavior_id);

// Delete a skill from the database.
void delete_skill(CdClientData& data, const std::string& db_path, int skill_id);

// --- Raw insert/delete helpers for undo/redo (insert with specific IDs) ---

// Insert a behavior with all fields specified (for redo re-insertion).
void insert_behavior_raw(CdClientData& data, const std::string& db_path,
                         const BehaviorTemplate& bt);

// Insert a single parameter (for redo re-insertion).
void insert_parameter_raw(CdClientData& data, const std::string& db_path,
                          const BehaviorParameter& bp);

// Insert a skill with all fields specified (for redo re-insertion).
void insert_skill_raw(CdClientData& data, const std::string& db_path,
                      const SkillBehavior& sb);

// Delete a single parameter from a behavior (not all params).
void delete_parameter(CdClientData& data, const std::string& db_path,
                      int behavior_id, const std::string& parameter_id);

// Save a subtree as a named JSON template file.
void save_template(const CdClientData& data, int root_behavior_id,
                   const std::string& template_path);

// Load a template and insert it into the database. Returns new root behaviorID.
int load_template(CdClientData& data, const std::string& db_path,
                  const std::string& template_path);

} // namespace skill_editor
