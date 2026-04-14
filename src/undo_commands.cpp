#include "undo_commands.h"

#include <sqlite3.h>
#include <unordered_set>

namespace skill_editor {

// Helper: collect all behavior IDs in a skill's tree via BFS.
static std::unordered_set<int> collect_tree(const CdClientData& data, int skill_id) {
    int root_id = 0;
    for (const auto& s : data.skills)
        if (s.skill_id == skill_id) { root_id = s.behavior_id; break; }
    std::unordered_set<int> in_tree;
    if (root_id == 0) return in_tree;
    std::vector<int> queue = {root_id};
    while (!queue.empty()) {
        int bid = queue.back(); queue.pop_back();
        if (!in_tree.insert(bid).second) continue;
        auto pit = data.params.find(bid);
        if (pit == data.params.end()) continue;
        for (const auto& p : pit->second) {
            int ref = static_cast<int>(p.value);
            if (ref > 0 && data.behaviors.count(ref) && is_behavior_ref_param(p.parameter_id))
                queue.push_back(ref);
        }
    }
    return in_tree;
}

// Helper: snapshot all behaviors created by a clone/template operation.
// Call after clone_subtree or load_template to capture what was created.
static void snapshot_created(const CdClientData& data, int root_id,
                             std::vector<BehaviorTemplate>& out_behaviors,
                             std::unordered_map<int, std::vector<BehaviorParameter>>& out_params) {
    out_behaviors.clear();
    out_params.clear();
    // BFS from root_id to collect all nodes in the cloned tree
    std::vector<int> queue = {root_id};
    std::unordered_set<int> visited;
    while (!queue.empty()) {
        int bid = queue.back(); queue.pop_back();
        if (!visited.insert(bid).second) continue;
        auto bit = data.behaviors.find(bid);
        if (bit == data.behaviors.end()) continue;
        out_behaviors.push_back(bit->second);
        auto pit = data.params.find(bid);
        if (pit != data.params.end()) {
            out_params[bid] = pit->second;
            for (const auto& p : pit->second) {
                int ref = static_cast<int>(p.value);
                if (ref > 0 && data.behaviors.count(ref) && is_behavior_ref_param(p.parameter_id))
                    queue.push_back(ref);
            }
        }
    }
}

// Helper: delete all behaviors in a snapshot from DB + in-memory.
static void delete_snapshot(CdClientData& data, const std::string& db_path,
                            const std::vector<BehaviorTemplate>& behaviors) {
    for (const auto& bt : behaviors)
        delete_behavior(data, db_path, bt.behavior_id);
}

// Helper: re-insert all behaviors + params from a snapshot.
static void reinsert_snapshot(CdClientData& data, const std::string& db_path,
                              const std::vector<BehaviorTemplate>& behaviors,
                              const std::unordered_map<int, std::vector<BehaviorParameter>>& params) {
    for (const auto& bt : behaviors)
        insert_behavior_raw(data, db_path, bt);
    for (const auto& [bid, pvec] : params)
        for (const auto& bp : pvec)
            insert_parameter_raw(data, db_path, bp);
}

// ===========================================================================
// 1. SaveParameterCommand
// ===========================================================================

SaveParameterCommand::SaveParameterCommand(
    CdClientData& data, const std::string& db_path,
    int behavior_id, const std::string& param_id,
    double old_value, double new_value, bool was_insert,
    RefreshCallback refresh)
    : QUndoCommand(QString("Change %1 on behavior %2")
          .arg(QString::fromStdString(param_id)).arg(behavior_id))
    , data_(data), db_path_(db_path), behavior_id_(behavior_id)
    , param_id_(param_id), old_value_(old_value), new_value_(new_value)
    , was_insert_(was_insert), refresh_(refresh) {}

void SaveParameterCommand::redo() {
    if (first_redo_) { first_redo_ = false; return; } // Already applied by caller
    save_parameter(data_, db_path_, behavior_id_, param_id_, new_value_);
    if (refresh_) refresh_(behavior_id_);
}

void SaveParameterCommand::undo() {
    if (was_insert_) {
        delete_parameter(data_, db_path_, behavior_id_, param_id_);
    } else {
        save_parameter(data_, db_path_, behavior_id_, param_id_, old_value_);
    }
    if (refresh_) refresh_(behavior_id_);
}

bool SaveParameterCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const SaveParameterCommand*>(other);
    if (!o) return false;
    if (o->behavior_id_ != behavior_id_ || o->param_id_ != param_id_) return false;
    new_value_ = o->new_value_;
    return true;
}

// ===========================================================================
// 2. SaveEffectCommand
// ===========================================================================

SaveEffectCommand::SaveEffectCommand(
    CdClientData& data, const std::string& db_path,
    int behavior_id,
    int old_effect_id, const std::string& old_effect_handle,
    int new_effect_id, const std::string& new_effect_handle,
    RefreshCallback refresh)
    : QUndoCommand(QString("Change effect on behavior %1").arg(behavior_id))
    , data_(data), db_path_(db_path), behavior_id_(behavior_id)
    , old_effect_id_(old_effect_id), new_effect_id_(new_effect_id)
    , old_effect_handle_(old_effect_handle), new_effect_handle_(new_effect_handle)
    , refresh_(refresh) {}

void SaveEffectCommand::redo() {
    if (first_redo_) { first_redo_ = false; return; }
    save_effect(data_, db_path_, behavior_id_, new_effect_id_, new_effect_handle_);
    if (refresh_) refresh_(behavior_id_);
}

void SaveEffectCommand::undo() {
    save_effect(data_, db_path_, behavior_id_, old_effect_id_, old_effect_handle_);
    if (refresh_) refresh_(behavior_id_);
}

// ===========================================================================
// 3. CreateBehaviorCommand
// ===========================================================================

CreateBehaviorCommand::CreateBehaviorCommand(
    CdClientData& data, const std::string& db_path,
    int template_id, RefreshCallback refresh)
    : QUndoCommand(QString("Create behavior (template %1)").arg(template_id))
    , data_(data), db_path_(db_path), template_id_(template_id)
    , refresh_(refresh) {}

void CreateBehaviorCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        created_id_ = create_behavior(data_, db_path_, template_id_);
        snapshot_ = data_.behaviors[created_id_];
        return;
    }
    insert_behavior_raw(data_, db_path_, snapshot_);
    if (refresh_) refresh_(created_id_);
}

void CreateBehaviorCommand::undo() {
    delete_behavior(data_, db_path_, created_id_);
    if (refresh_) refresh_(0);
}

// ===========================================================================
// 4. CreateSkillCommand
// ===========================================================================

CreateSkillCommand::CreateSkillCommand(
    CdClientData& data, const std::string& db_path,
    int root_behavior_id, int imagination_cost, double cooldown,
    RefreshCallback refresh)
    : QUndoCommand(QString("Create skill"))
    , data_(data), db_path_(db_path), root_behavior_id_(root_behavior_id)
    , imagination_cost_(imagination_cost), cooldown_(cooldown)
    , refresh_(refresh) {}

void CreateSkillCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        created_id_ = create_skill(data_, db_path_, root_behavior_id_,
                                   imagination_cost_, cooldown_);
        for (const auto& s : data_.skills)
            if (s.skill_id == created_id_) { snapshot_ = s; break; }
        return;
    }
    insert_skill_raw(data_, db_path_, snapshot_);
    if (refresh_) refresh_(0);
}

void CreateSkillCommand::undo() {
    delete_skill(data_, db_path_, created_id_);
    if (refresh_) refresh_(0);
}

// ===========================================================================
// 5. DeleteBehaviorCommand
// ===========================================================================

DeleteBehaviorCommand::DeleteBehaviorCommand(
    CdClientData& data, const std::string& db_path,
    int behavior_id, RefreshCallback refresh)
    : QUndoCommand(QString("Delete behavior %1").arg(behavior_id))
    , data_(data), db_path_(db_path), behavior_id_(behavior_id)
    , refresh_(refresh) {}

void DeleteBehaviorCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        // Snapshot before deleting
        auto bit = data_.behaviors.find(behavior_id_);
        if (bit != data_.behaviors.end()) bt_snapshot_ = bit->second;
        auto pit = data_.params.find(behavior_id_);
        if (pit != data_.params.end()) params_snapshot_ = pit->second;
    }
    delete_behavior(data_, db_path_, behavior_id_);
    if (refresh_) refresh_(0);
}

void DeleteBehaviorCommand::undo() {
    insert_behavior_raw(data_, db_path_, bt_snapshot_);
    for (const auto& bp : params_snapshot_)
        insert_parameter_raw(data_, db_path_, bp);
    if (refresh_) refresh_(behavior_id_);
}

// ===========================================================================
// 6. DeleteSkillCommand
// ===========================================================================

DeleteSkillCommand::DeleteSkillCommand(
    CdClientData& data, const std::string& db_path,
    int skill_id, RefreshCallback refresh)
    : QUndoCommand(QString("Delete skill %1").arg(skill_id))
    , data_(data), db_path_(db_path), skill_id_(skill_id)
    , refresh_(refresh) {}

void DeleteSkillCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        for (const auto& s : data_.skills)
            if (s.skill_id == skill_id_) { snapshot_ = s; break; }
    }
    delete_skill(data_, db_path_, skill_id_);
    if (refresh_) refresh_(0);
}

void DeleteSkillCommand::undo() {
    insert_skill_raw(data_, db_path_, snapshot_);
    if (refresh_) refresh_(0);
}

// ===========================================================================
// 7. CloneSubtreeCommand
// ===========================================================================

CloneSubtreeCommand::CloneSubtreeCommand(
    CdClientData& data, const std::string& db_path,
    int source_behavior_id, RefreshCallback refresh)
    : QUndoCommand(QString("Clone subtree from behavior %1").arg(source_behavior_id))
    , data_(data), db_path_(db_path), source_behavior_id_(source_behavior_id)
    , refresh_(refresh) {}

void CloneSubtreeCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        cloned_root_id_ = clone_subtree(data_, db_path_, source_behavior_id_);
        snapshot_created(data_, cloned_root_id_, created_behaviors_, created_params_);
        return;
    }
    reinsert_snapshot(data_, db_path_, created_behaviors_, created_params_);
    if (refresh_) refresh_(cloned_root_id_);
}

void CloneSubtreeCommand::undo() {
    delete_snapshot(data_, db_path_, created_behaviors_);
    if (refresh_) refresh_(source_behavior_id_);
}

// ===========================================================================
// 8. CloneSkillCommand
// ===========================================================================

CloneSkillCommand::CloneSkillCommand(
    CdClientData& data, const std::string& db_path,
    int source_skill_id, RefreshCallback refresh)
    : QUndoCommand(QString("Clone skill %1").arg(source_skill_id))
    , data_(data), db_path_(db_path), source_skill_id_(source_skill_id)
    , refresh_(refresh) {}

void CloneSkillCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        created_skill_id_ = clone_skill(data_, db_path_, source_skill_id_);
        // Find the cloned root
        for (const auto& s : data_.skills)
            if (s.skill_id == created_skill_id_) {
                skill_snapshot_ = s;
                cloned_root_id_ = s.behavior_id;
                break;
            }
        snapshot_created(data_, cloned_root_id_, created_behaviors_, created_params_);
        return;
    }
    reinsert_snapshot(data_, db_path_, created_behaviors_, created_params_);
    insert_skill_raw(data_, db_path_, skill_snapshot_);
    if (refresh_) refresh_(0);
}

void CloneSkillCommand::undo() {
    delete_skill(data_, db_path_, created_skill_id_);
    delete_snapshot(data_, db_path_, created_behaviors_);
    if (refresh_) refresh_(0);
}

// ===========================================================================
// 9. LoadTemplateCommand
// ===========================================================================

LoadTemplateCommand::LoadTemplateCommand(
    CdClientData& data, const std::string& db_path,
    const std::string& template_path, RefreshCallback refresh)
    : QUndoCommand(QString("Load template"))
    , data_(data), db_path_(db_path), template_path_(template_path)
    , refresh_(refresh) {}

void LoadTemplateCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        created_root_id_ = load_template(data_, db_path_, template_path_);
        snapshot_created(data_, created_root_id_, created_behaviors_, created_params_);
        return;
    }
    reinsert_snapshot(data_, db_path_, created_behaviors_, created_params_);
    if (refresh_) refresh_(created_root_id_);
}

void LoadTemplateCommand::undo() {
    delete_snapshot(data_, db_path_, created_behaviors_);
    if (refresh_) refresh_(0);
}

// ===========================================================================
// 10. CowParameterEditCommand
// ===========================================================================

CowParameterEditCommand::CowParameterEditCommand(
    CdClientData& data, const std::string& db_path,
    int skill_id, int original_behavior_id,
    const std::string& param_id, double new_value,
    RefreshCallback refresh)
    : QUndoCommand(QString("Edit %1 (copy-on-write)")
          .arg(QString::fromStdString(param_id)))
    , data_(data), db_path_(db_path), skill_id_(skill_id)
    , original_behavior_id_(original_behavior_id)
    , param_id_(param_id), new_value_(new_value)
    , refresh_(refresh) {}

void CowParameterEditCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;

        // Capture old param value
        old_param_value_ = 0;
        auto pit = data_.params.find(original_behavior_id_);
        if (pit != data_.params.end())
            for (const auto& p : pit->second)
                if (p.parameter_id == param_id_) { old_param_value_ = p.value; break; }

        // Save old skill root
        old_skill_root_ = 0;
        for (const auto& s : data_.skills)
            if (s.skill_id == skill_id_) { old_skill_root_ = s.behavior_id; break; }

        // Clone subtree
        cloned_root_id_ = clone_subtree(data_, db_path_, original_behavior_id_);
        snapshot_created(data_, cloned_root_id_, created_behaviors_, created_params_);

        // Capture tree before rewire to track changes
        auto in_tree = collect_tree(data_, skill_id_);

        // Rewire: update skill root if needed
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_ && s.behavior_id == original_behavior_id_) {
                s.behavior_id = cloned_root_id_;
                // DB update
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(cloned_root_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }

        // Rewire parent params in tree
        rewire_entries_.clear();
        for (int bid : in_tree) {
            auto pit2 = data_.params.find(bid);
            if (pit2 == data_.params.end()) continue;
            for (auto& p : pit2->second) {
                if (static_cast<int>(p.value) == original_behavior_id_ &&
                    is_behavior_ref_param(p.parameter_id)) {
                    rewire_entries_.push_back({bid, p.parameter_id, p.value});
                    p.value = static_cast<double>(cloned_root_id_);
                    save_parameter(data_, db_path_, bid, p.parameter_id, p.value);
                }
            }
        }

        // Now save the actual parameter edit on the cloned behavior
        // Find the cloned equivalent of the original behavior
        // (if original was the root, cloned_root_id_ IS the equivalent;
        //  otherwise we need to find the mapping)
        save_parameter(data_, db_path_, cloned_root_id_, param_id_, new_value_);
        return;
    }

    // Subsequent redo: re-insert cloned behaviors, rewire, save param
    reinsert_snapshot(data_, db_path_, created_behaviors_, created_params_);
    // Rewire skill root
    if (old_skill_root_ == original_behavior_id_) {
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_) {
                s.behavior_id = cloned_root_id_;
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(cloned_root_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }
    }
    // Rewire parent refs
    for (const auto& re : rewire_entries_)
        save_parameter(data_, db_path_, re.parent_id, re.param_name,
                       static_cast<double>(cloned_root_id_));
    // Apply param edit
    save_parameter(data_, db_path_, cloned_root_id_, param_id_, new_value_);
    if (refresh_) refresh_(cloned_root_id_);
}

void CowParameterEditCommand::undo() {
    // Undo param edit (on cloned behavior — will be deleted anyway)
    // Undo rewires
    for (const auto& re : rewire_entries_)
        save_parameter(data_, db_path_, re.parent_id, re.param_name, re.old_value);
    // Undo skill root rewire
    if (old_skill_root_ == original_behavior_id_) {
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_) {
                s.behavior_id = original_behavior_id_;
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(original_behavior_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }
    }
    // Delete all cloned behaviors
    delete_snapshot(data_, db_path_, created_behaviors_);
    if (refresh_) refresh_(original_behavior_id_);
}

// ===========================================================================
// 11. CowEffectEditCommand
// ===========================================================================

CowEffectEditCommand::CowEffectEditCommand(
    CdClientData& data, const std::string& db_path,
    int skill_id, int original_behavior_id,
    int new_effect_id, const std::string& new_effect_handle,
    RefreshCallback refresh)
    : QUndoCommand(QString("Edit effect (copy-on-write)"))
    , data_(data), db_path_(db_path), skill_id_(skill_id)
    , original_behavior_id_(original_behavior_id)
    , new_effect_id_(new_effect_id), new_effect_handle_(new_effect_handle)
    , refresh_(refresh) {}

void CowEffectEditCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;

        // Capture old effect
        auto bit = data_.behaviors.find(original_behavior_id_);
        if (bit != data_.behaviors.end()) {
            old_effect_id_ = bit->second.effect_id;
            old_effect_handle_ = bit->second.effect_handle;
        }

        old_skill_root_ = 0;
        for (const auto& s : data_.skills)
            if (s.skill_id == skill_id_) { old_skill_root_ = s.behavior_id; break; }

        cloned_root_id_ = clone_subtree(data_, db_path_, original_behavior_id_);
        snapshot_created(data_, cloned_root_id_, created_behaviors_, created_params_);

        auto in_tree = collect_tree(data_, skill_id_);

        // Rewire skill root
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_ && s.behavior_id == original_behavior_id_) {
                s.behavior_id = cloned_root_id_;
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(cloned_root_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }

        // Rewire parent params
        rewire_entries_.clear();
        for (int bid : in_tree) {
            auto pit = data_.params.find(bid);
            if (pit == data_.params.end()) continue;
            for (auto& p : pit->second) {
                if (static_cast<int>(p.value) == original_behavior_id_ &&
                    is_behavior_ref_param(p.parameter_id)) {
                    rewire_entries_.push_back({bid, p.parameter_id, p.value});
                    p.value = static_cast<double>(cloned_root_id_);
                    save_parameter(data_, db_path_, bid, p.parameter_id, p.value);
                }
            }
        }

        save_effect(data_, db_path_, cloned_root_id_, new_effect_id_, new_effect_handle_);
        return;
    }

    reinsert_snapshot(data_, db_path_, created_behaviors_, created_params_);
    if (old_skill_root_ == original_behavior_id_) {
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_) {
                s.behavior_id = cloned_root_id_;
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(cloned_root_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }
    }
    for (const auto& re : rewire_entries_)
        save_parameter(data_, db_path_, re.parent_id, re.param_name,
                       static_cast<double>(cloned_root_id_));
    save_effect(data_, db_path_, cloned_root_id_, new_effect_id_, new_effect_handle_);
    if (refresh_) refresh_(cloned_root_id_);
}

void CowEffectEditCommand::undo() {
    for (const auto& re : rewire_entries_)
        save_parameter(data_, db_path_, re.parent_id, re.param_name, re.old_value);
    if (old_skill_root_ == original_behavior_id_) {
        for (auto& s : data_.skills) {
            if (s.skill_id == skill_id_) {
                s.behavior_id = original_behavior_id_;
                sqlite3* raw = nullptr;
                sqlite3_open(db_path_.c_str(), &raw);
                std::string sql = "UPDATE SkillBehavior SET behaviorID = " +
                    std::to_string(original_behavior_id_) + " WHERE skillID = " +
                    std::to_string(skill_id_);
                sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr);
                sqlite3_close(raw);
                break;
            }
        }
    }
    delete_snapshot(data_, db_path_, created_behaviors_);
    if (refresh_) refresh_(original_behavior_id_);
}

// ===========================================================================
// 12. AddDynamicParamCommand
// ===========================================================================

AddDynamicParamCommand::AddDynamicParamCommand(
    CdClientData& data, const std::string& db_path,
    int behavior_id,
    const std::vector<std::string>& param_names,
    RefreshCallback refresh)
    : QUndoCommand(QString("Add %1").arg(QString::fromStdString(
          param_names.empty() ? "" : param_names[0])))
    , data_(data), db_path_(db_path), behavior_id_(behavior_id)
    , param_names_(param_names), refresh_(refresh) {}

void AddDynamicParamCommand::redo() {
    if (first_redo_) { first_redo_ = false; return; }
    for (const auto& name : param_names_)
        save_parameter(data_, db_path_, behavior_id_, name, 0.0);
    if (refresh_) refresh_(behavior_id_);
}

void AddDynamicParamCommand::undo() {
    for (const auto& name : param_names_)
        delete_parameter(data_, db_path_, behavior_id_, name);
    if (refresh_) refresh_(behavior_id_);
}

// ===========================================================================
// 13. RemoveDynamicParamCommand
// ===========================================================================

RemoveDynamicParamCommand::RemoveDynamicParamCommand(
    CdClientData& data, const std::string& db_path,
    int behavior_id,
    const std::vector<std::string>& param_names,
    RefreshCallback refresh)
    : QUndoCommand(QString("Remove %1").arg(QString::fromStdString(
          param_names.empty() ? "" : param_names[0])))
    , data_(data), db_path_(db_path), behavior_id_(behavior_id)
    , param_names_(param_names), refresh_(refresh) {}

void RemoveDynamicParamCommand::redo() {
    if (first_redo_) {
        first_redo_ = false;
        // Snapshot old values before deleting
        old_values_.resize(param_names_.size(), 0.0);
        auto pit = data_.params.find(behavior_id_);
        if (pit != data_.params.end()) {
            for (size_t i = 0; i < param_names_.size(); ++i)
                for (const auto& p : pit->second)
                    if (p.parameter_id == param_names_[i])
                        { old_values_[i] = p.value; break; }
        }
    }
    for (const auto& name : param_names_)
        delete_parameter(data_, db_path_, behavior_id_, name);
    if (refresh_) refresh_(behavior_id_);
}

void RemoveDynamicParamCommand::undo() {
    for (size_t i = 0; i < param_names_.size(); ++i) {
        BehaviorParameter bp;
        bp.behavior_id = behavior_id_;
        bp.parameter_id = param_names_[i];
        bp.value = i < old_values_.size() ? old_values_[i] : 0.0;
        insert_parameter_raw(data_, db_path_, bp);
    }
    if (refresh_) refresh_(behavior_id_);
}

} // namespace skill_editor
