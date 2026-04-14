#pragma once
// undo_commands.h — QUndoCommand subclasses for all skill editor mutations.
//
// Each command stores enough data to redo() and undo() both the in-memory
// CdClientData and the SQLite database.

#include "db_source.h"

#include <QUndoCommand>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace skill_editor {

// Callback invoked after every undo/redo to refresh the UI.
using RefreshCallback = std::function<void(int select_behavior_id)>;

// ---------------------------------------------------------------------------
// 1. SaveParameterCommand
// ---------------------------------------------------------------------------
class SaveParameterCommand : public QUndoCommand {
public:
    SaveParameterCommand(CdClientData& data, const std::string& db_path,
                         int behavior_id, const std::string& param_id,
                         double old_value, double new_value, bool was_insert,
                         RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    CdClientData& data_;
    std::string db_path_;
    int behavior_id_;
    std::string param_id_;
    double old_value_;
    double new_value_;
    bool was_insert_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 2. SaveEffectCommand
// ---------------------------------------------------------------------------
class SaveEffectCommand : public QUndoCommand {
public:
    SaveEffectCommand(CdClientData& data, const std::string& db_path,
                      int behavior_id,
                      int old_effect_id, const std::string& old_effect_handle,
                      int new_effect_id, const std::string& new_effect_handle,
                      RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    int behavior_id_;
    int old_effect_id_, new_effect_id_;
    std::string old_effect_handle_, new_effect_handle_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 3. CreateBehaviorCommand
// ---------------------------------------------------------------------------
class CreateBehaviorCommand : public QUndoCommand {
public:
    CreateBehaviorCommand(CdClientData& data, const std::string& db_path,
                          int template_id, RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int created_id() const { return created_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int template_id_;
    int created_id_ = 0;
    BehaviorTemplate snapshot_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 4. CreateSkillCommand
// ---------------------------------------------------------------------------
class CreateSkillCommand : public QUndoCommand {
public:
    CreateSkillCommand(CdClientData& data, const std::string& db_path,
                       int root_behavior_id, int imagination_cost, double cooldown,
                       RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int created_id() const { return created_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int root_behavior_id_;
    int imagination_cost_;
    double cooldown_;
    int created_id_ = 0;
    SkillBehavior snapshot_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 5. DeleteBehaviorCommand
// ---------------------------------------------------------------------------
class DeleteBehaviorCommand : public QUndoCommand {
public:
    DeleteBehaviorCommand(CdClientData& data, const std::string& db_path,
                          int behavior_id, RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    int behavior_id_;
    BehaviorTemplate bt_snapshot_;
    std::vector<BehaviorParameter> params_snapshot_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 6. DeleteSkillCommand
// ---------------------------------------------------------------------------
class DeleteSkillCommand : public QUndoCommand {
public:
    DeleteSkillCommand(CdClientData& data, const std::string& db_path,
                       int skill_id, RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    int skill_id_;
    SkillBehavior snapshot_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 7. CloneSubtreeCommand
// ---------------------------------------------------------------------------
class CloneSubtreeCommand : public QUndoCommand {
public:
    CloneSubtreeCommand(CdClientData& data, const std::string& db_path,
                        int source_behavior_id, RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int cloned_root_id() const { return cloned_root_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int source_behavior_id_;
    int cloned_root_id_ = 0;
    std::vector<BehaviorTemplate> created_behaviors_;
    std::unordered_map<int, std::vector<BehaviorParameter>> created_params_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 8. CloneSkillCommand
// ---------------------------------------------------------------------------
class CloneSkillCommand : public QUndoCommand {
public:
    CloneSkillCommand(CdClientData& data, const std::string& db_path,
                      int source_skill_id, RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int created_skill_id() const { return created_skill_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int source_skill_id_;
    int created_skill_id_ = 0;
    int cloned_root_id_ = 0;
    SkillBehavior skill_snapshot_;
    std::vector<BehaviorTemplate> created_behaviors_;
    std::unordered_map<int, std::vector<BehaviorParameter>> created_params_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 9. LoadTemplateCommand
// ---------------------------------------------------------------------------
class LoadTemplateCommand : public QUndoCommand {
public:
    LoadTemplateCommand(CdClientData& data, const std::string& db_path,
                        const std::string& template_path, RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int created_root_id() const { return created_root_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    std::string template_path_;
    int created_root_id_ = 0;
    std::vector<BehaviorTemplate> created_behaviors_;
    std::unordered_map<int, std::vector<BehaviorParameter>> created_params_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 10. CowParameterEditCommand (compound: COW clone + rewire + param edit)
// ---------------------------------------------------------------------------
class CowParameterEditCommand : public QUndoCommand {
public:
    CowParameterEditCommand(CdClientData& data, const std::string& db_path,
                            int skill_id, int original_behavior_id,
                            const std::string& param_id, double new_value,
                            RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int cloned_root_id() const { return cloned_root_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int skill_id_;
    int original_behavior_id_;
    std::string param_id_;
    double new_value_;
    double old_param_value_;

    // COW clone data
    int cloned_root_id_ = 0;
    std::vector<BehaviorTemplate> created_behaviors_;
    std::unordered_map<int, std::vector<BehaviorParameter>> created_params_;

    // Rewire data: stores (parent_behavior_id, param_name, old_value) for each rewired ref
    struct RewireEntry {
        int parent_id;
        std::string param_name;
        double old_value;
    };
    std::vector<RewireEntry> rewire_entries_;
    int old_skill_root_ = 0; // if skill root was rewired

    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 11. CowEffectEditCommand (compound: COW clone + rewire + effect edit)
// ---------------------------------------------------------------------------
class CowEffectEditCommand : public QUndoCommand {
public:
    CowEffectEditCommand(CdClientData& data, const std::string& db_path,
                         int skill_id, int original_behavior_id,
                         int new_effect_id, const std::string& new_effect_handle,
                         RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int cloned_root_id() const { return cloned_root_id_; }

private:
    CdClientData& data_;
    std::string db_path_;
    int skill_id_;
    int original_behavior_id_;
    int new_effect_id_;
    std::string new_effect_handle_;
    int old_effect_id_;
    std::string old_effect_handle_;

    int cloned_root_id_ = 0;
    std::vector<BehaviorTemplate> created_behaviors_;
    std::unordered_map<int, std::vector<BehaviorParameter>> created_params_;

    struct RewireEntry {
        int parent_id;
        std::string param_name;
        double old_value;
    };
    std::vector<RewireEntry> rewire_entries_;
    int old_skill_root_ = 0;

    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 12. AddDynamicParamCommand — add a group of params with value 0
// ---------------------------------------------------------------------------
class AddDynamicParamCommand : public QUndoCommand {
public:
    AddDynamicParamCommand(CdClientData& data, const std::string& db_path,
                           int behavior_id,
                           const std::vector<std::string>& param_names,
                           RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    int behavior_id_;
    std::vector<std::string> param_names_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 13. RemoveDynamicParamCommand — remove a group of params
// ---------------------------------------------------------------------------
class RemoveDynamicParamCommand : public QUndoCommand {
public:
    RemoveDynamicParamCommand(CdClientData& data, const std::string& db_path,
                              int behavior_id,
                              const std::vector<std::string>& param_names,
                              RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    int behavior_id_;
    std::vector<std::string> param_names_;
    std::vector<double> old_values_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 14. AddObjectSkillCommand — assign a skill to an object
// ---------------------------------------------------------------------------
class AddObjectSkillCommand : public QUndoCommand {
public:
    AddObjectSkillCommand(CdClientData& data, const std::string& db_path,
                          int object_template, int skill_id, int cast_on_type,
                          RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    ObjectSkill os_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 15. RemoveObjectSkillCommand — remove a skill from an object
// ---------------------------------------------------------------------------
class RemoveObjectSkillCommand : public QUndoCommand {
public:
    RemoveObjectSkillCommand(CdClientData& data, const std::string& db_path,
                             int object_template, int skill_id,
                             RefreshCallback refresh);
    void redo() override;
    void undo() override;

private:
    CdClientData& data_;
    std::string db_path_;
    ObjectSkill snapshot_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

// ---------------------------------------------------------------------------
// 16. EditObjectSkillCastTypeCommand — change castOnType for an assignment
// ---------------------------------------------------------------------------
class EditObjectSkillCastTypeCommand : public QUndoCommand {
public:
    EditObjectSkillCastTypeCommand(CdClientData& data, const std::string& db_path,
                                   int object_template, int skill_id,
                                   int old_cast_on_type, int new_cast_on_type,
                                   RefreshCallback refresh);
    void redo() override;
    void undo() override;
    int id() const override { return 2; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    CdClientData& data_;
    std::string db_path_;
    int object_template_;
    int skill_id_;
    int old_cast_on_type_;
    int new_cast_on_type_;
    RefreshCallback refresh_;
    bool first_redo_ = true;
};

} // namespace skill_editor
