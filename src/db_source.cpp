#include "db_source.h"

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace skill_editor {

// ---------------------------------------------------------------------------
// CdClientData helpers
// ---------------------------------------------------------------------------

std::string CdClientData::behavior_type_name(int behavior_id) const {
    auto bit = behaviors.find(behavior_id);
    if (bit == behaviors.end()) return "???";
    auto nit = template_names.find(bit->second.template_id);
    if (nit == template_names.end()) return "Unknown(" + std::to_string(bit->second.template_id) + ")";
    return nit->second.name;
}

std::vector<std::pair<std::string, int>> CdClientData::child_behaviors(int behavior_id) const {
    // Only parameters whose NAME is a known forward behavior reference should be
    // treated as child links. Back-references (End→Start, AlterChainDelay→Chain)
    // are excluded to prevent visual cycles in the tree.
    std::vector<std::pair<std::string, int>> children;
    auto pit = params.find(behavior_id);
    if (pit == params.end()) return children;

    // Get this behavior's template ID for back-ref checking
    int tmpl_id = 0;
    auto bit = behaviors.find(behavior_id);
    if (bit != behaviors.end()) tmpl_id = bit->second.template_id;

    for (const auto& p : pit->second) {
        if (!is_behavior_ref_param(p.parameter_id)) continue;
        if (is_back_ref_param(p.parameter_id, tmpl_id)) continue;
        int child_id = static_cast<int>(p.value);
        if (child_id > 0 && behaviors.count(child_id)) {
            children.emplace_back(p.parameter_id, child_id);
        }
    }
    std::sort(children.begin(), children.end());
    return children;
}

// ---------------------------------------------------------------------------
// SQLite loader
// ---------------------------------------------------------------------------

namespace {

struct SqliteDeleter {
    void operator()(sqlite3* db) { sqlite3_close(db); }
    void operator()(sqlite3_stmt* stmt) { sqlite3_finalize(stmt); }
};

using SqliteDb = std::unique_ptr<sqlite3, SqliteDeleter>;
using SqliteStmt = std::unique_ptr<sqlite3_stmt, SqliteDeleter>;

SqliteStmt prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQL error: ") + sqlite3_errmsg(db));
    }
    return SqliteStmt(raw);
}

std::string col_text(sqlite3_stmt* s, int col) {
    auto p = sqlite3_column_text(s, col);
    return p ? std::string(reinterpret_cast<const char*>(p)) : "";
}

} // anonymous namespace

CdClientData load_from_sqlite(const std::string& path) {
    sqlite3* raw_db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &raw_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Cannot open SQLite: " + path);
    }
    SqliteDb db(raw_db);
    CdClientData data;

    // BehaviorTemplateName
    {
        auto stmt = prepare(db.get(),
            "SELECT templateID, name FROM BehaviorTemplateName");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            BehaviorTemplateName tn;
            tn.template_id = sqlite3_column_int(stmt.get(), 0);
            tn.name = col_text(stmt.get(), 1);
            data.template_names[tn.template_id] = std::move(tn);
        }
    }

    // BehaviorTemplate
    {
        auto stmt = prepare(db.get(),
            "SELECT behaviorID, templateID, effectID, effectHandle FROM BehaviorTemplate");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            BehaviorTemplate bt;
            bt.behavior_id = sqlite3_column_int(stmt.get(), 0);
            bt.template_id = sqlite3_column_int(stmt.get(), 1);
            bt.effect_id = sqlite3_column_int(stmt.get(), 2);
            bt.effect_handle = col_text(stmt.get(), 3);
            data.behaviors[bt.behavior_id] = std::move(bt);
        }
    }

    // BehaviorParameter
    {
        auto stmt = prepare(db.get(),
            "SELECT behaviorID, parameterID, value FROM BehaviorParameter");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            BehaviorParameter bp;
            bp.behavior_id = sqlite3_column_int(stmt.get(), 0);
            bp.parameter_id = col_text(stmt.get(), 1);
            bp.value = sqlite3_column_double(stmt.get(), 2);
            data.params[bp.behavior_id].push_back(std::move(bp));
        }
    }

    // SkillBehavior
    {
        auto stmt = prepare(db.get(),
            "SELECT skillID, behaviorID, imaginationcost, cooldown, cooldowngroup, "
            "skillIcon, gate_version FROM SkillBehavior");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            SkillBehavior sb;
            sb.skill_id = sqlite3_column_int(stmt.get(), 0);
            sb.behavior_id = sqlite3_column_int(stmt.get(), 1);
            sb.imagination_cost = sqlite3_column_int(stmt.get(), 2);
            sb.cooldown = sqlite3_column_double(stmt.get(), 3);
            sb.cooldown_group = sqlite3_column_int(stmt.get(), 4);
            sb.skill_icon = sqlite3_column_int(stmt.get(), 5);
            sb.gate_version = col_text(stmt.get(), 6);
            data.skills.push_back(std::move(sb));
        }
    }

    // Sort skills by ID
    std::sort(data.skills.begin(), data.skills.end(),
              [](const auto& a, const auto& b) { return a.skill_id < b.skill_id; });

    // ObjectSkills — which objects use which skills
    {
        auto stmt = prepare(db.get(),
            "SELECT objectTemplate, skillID, castOnType FROM ObjectSkills");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            ObjectSkill os;
            os.object_template = sqlite3_column_int(stmt.get(), 0);
            os.skill_id = sqlite3_column_int(stmt.get(), 1);
            os.cast_on_type = sqlite3_column_int(stmt.get(), 2);
            data.skill_objects[os.skill_id].push_back(os);
        }
    }

    // Object names from Objects table
    {
        auto stmt = prepare(db.get(),
            "SELECT id, name FROM Objects");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt.get(), 0);
            data.object_names[id] = col_text(stmt.get(), 1);
        }
    }

    return data;
}

// ---------------------------------------------------------------------------
// Create / clone / template operations
// ---------------------------------------------------------------------------

static int next_id(sqlite3* db, const char* table, const char* id_col) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT MAX(%s) FROM %s", id_col, table);
    sqlite3_stmt* raw = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    SqliteStmt stmt(raw);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW)
        return sqlite3_column_int(stmt.get(), 0) + 1;
    return 1;
}

int create_behavior(CdClientData& data, const std::string& db_path, int template_id) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    int new_id = next_id(db.get(), "BehaviorTemplate", "behaviorID");

    auto stmt = prepare(db.get(),
        "INSERT INTO BehaviorTemplate (behaviorID, templateID, effectID, effectHandle) "
        "VALUES (?, ?, 0, '')");
    sqlite3_bind_int(stmt.get(), 1, new_id);
    sqlite3_bind_int(stmt.get(), 2, template_id);
    sqlite3_step(stmt.get());

    BehaviorTemplate bt;
    bt.behavior_id = new_id;
    bt.template_id = template_id;
    data.behaviors[new_id] = bt;

    return new_id;
}

int create_skill(CdClientData& data, const std::string& db_path, int root_behavior_id,
                 int imagination_cost, double cooldown) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    int new_id = next_id(db.get(), "SkillBehavior", "skillID");

    auto stmt = prepare(db.get(),
        "INSERT INTO SkillBehavior (skillID, behaviorID, imaginationcost, cooldown, "
        "cooldowngroup, inNpcEditor, skillIcon, locStatus) "
        "VALUES (?, ?, ?, ?, 0, 0, 0, 0)");
    sqlite3_bind_int(stmt.get(), 1, new_id);
    sqlite3_bind_int(stmt.get(), 2, root_behavior_id);
    sqlite3_bind_int(stmt.get(), 3, imagination_cost);
    sqlite3_bind_double(stmt.get(), 4, cooldown);
    sqlite3_step(stmt.get());

    SkillBehavior sb;
    sb.skill_id = new_id;
    sb.behavior_id = root_behavior_id;
    sb.imagination_cost = imagination_cost;
    sb.cooldown = cooldown;
    data.skills.push_back(sb);
    std::sort(data.skills.begin(), data.skills.end(),
              [](const auto& a, const auto& b) { return a.skill_id < b.skill_id; });

    return new_id;
}

int clone_subtree(CdClientData& data, const std::string& db_path, int source_behavior_id) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    // Map old IDs to new IDs
    std::unordered_map<int, int> id_map;
    std::function<int(int)> clone_node = [&](int old_id) -> int {
        if (id_map.count(old_id)) return id_map[old_id];

        int new_id = next_id(db.get(), "BehaviorTemplate", "behaviorID");
        id_map[old_id] = new_id;

        // Copy BehaviorTemplate row
        auto bit = data.behaviors.find(old_id);
        int tmpl_id = bit != data.behaviors.end() ? bit->second.template_id : 0;
        int eff_id = bit != data.behaviors.end() ? bit->second.effect_id : 0;
        std::string eff_handle = bit != data.behaviors.end() ? bit->second.effect_handle : "";

        {
            auto stmt = prepare(db.get(),
                "INSERT INTO BehaviorTemplate (behaviorID, templateID, effectID, effectHandle) "
                "VALUES (?, ?, ?, ?)");
            sqlite3_bind_int(stmt.get(), 1, new_id);
            sqlite3_bind_int(stmt.get(), 2, tmpl_id);
            sqlite3_bind_int(stmt.get(), 3, eff_id);
            sqlite3_bind_text(stmt.get(), 4, eff_handle.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt.get());
        }

        BehaviorTemplate new_bt;
        new_bt.behavior_id = new_id;
        new_bt.template_id = tmpl_id;
        new_bt.effect_id = eff_id;
        new_bt.effect_handle = eff_handle;
        data.behaviors[new_id] = new_bt;

        // Copy parameters, recursively cloning child behavior references
        auto pit = data.params.find(old_id);
        if (pit != data.params.end()) {
            for (const auto& p : pit->second) {
                double new_val = p.value;
                int ref = static_cast<int>(p.value);
                if (ref > 0 && data.behaviors.count(ref)) {
                    new_val = static_cast<double>(clone_node(ref));
                }
                {
                    auto stmt = prepare(db.get(),
                        "INSERT INTO BehaviorParameter (behaviorID, parameterID, value) "
                        "VALUES (?, ?, ?)");
                    sqlite3_bind_int(stmt.get(), 1, new_id);
                    sqlite3_bind_text(stmt.get(), 2, p.parameter_id.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_double(stmt.get(), 3, new_val);
                    sqlite3_step(stmt.get());
                }
                BehaviorParameter new_bp;
                new_bp.behavior_id = new_id;
                new_bp.parameter_id = p.parameter_id;
                new_bp.value = new_val;
                data.params[new_id].push_back(new_bp);
            }
        }

        return new_id;
    };

    return clone_node(source_behavior_id);
}

int clone_skill(CdClientData& data, const std::string& db_path, int source_skill_id) {
    int root_beh = 0;
    int imag = 0;
    double cd = 0;
    for (const auto& s : data.skills) {
        if (s.skill_id == source_skill_id) {
            root_beh = s.behavior_id;
            imag = s.imagination_cost;
            cd = s.cooldown;
            break;
        }
    }
    if (root_beh == 0) return 0;

    int new_root = clone_subtree(data, db_path, root_beh);
    return create_skill(data, db_path, new_root, imag, cd);
}

// ---------------------------------------------------------------------------
// Delete operations
// ---------------------------------------------------------------------------

void delete_behavior(CdClientData& data, const std::string& db_path, int behavior_id) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    {
        auto stmt = prepare(db.get(), "DELETE FROM BehaviorTemplate WHERE behaviorID = ?");
        sqlite3_bind_int(stmt.get(), 1, behavior_id);
        sqlite3_step(stmt.get());
    }
    {
        auto stmt = prepare(db.get(), "DELETE FROM BehaviorParameter WHERE behaviorID = ?");
        sqlite3_bind_int(stmt.get(), 1, behavior_id);
        sqlite3_step(stmt.get());
    }

    data.behaviors.erase(behavior_id);
    data.params.erase(behavior_id);
}

void delete_skill(CdClientData& data, const std::string& db_path, int skill_id) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    {
        auto stmt = prepare(db.get(), "DELETE FROM SkillBehavior WHERE skillID = ?");
        sqlite3_bind_int(stmt.get(), 1, skill_id);
        sqlite3_step(stmt.get());
    }

    data.skills.erase(
        std::remove_if(data.skills.begin(), data.skills.end(),
                        [skill_id](const auto& s) { return s.skill_id == skill_id; }),
        data.skills.end());
}

// ---------------------------------------------------------------------------
// Raw insert/delete helpers for undo/redo
// ---------------------------------------------------------------------------

void insert_behavior_raw(CdClientData& data, const std::string& db_path,
                         const BehaviorTemplate& bt) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    auto stmt = prepare(db.get(),
        "INSERT INTO BehaviorTemplate (behaviorID, templateID, effectID, effectHandle) "
        "VALUES (?, ?, ?, ?)");
    sqlite3_bind_int(stmt.get(), 1, bt.behavior_id);
    sqlite3_bind_int(stmt.get(), 2, bt.template_id);
    sqlite3_bind_int(stmt.get(), 3, bt.effect_id);
    sqlite3_bind_text(stmt.get(), 4, bt.effect_handle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt.get());

    data.behaviors[bt.behavior_id] = bt;
}

void insert_parameter_raw(CdClientData& data, const std::string& db_path,
                          const BehaviorParameter& bp) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    auto stmt = prepare(db.get(),
        "INSERT INTO BehaviorParameter (behaviorID, parameterID, value) VALUES (?, ?, ?)");
    sqlite3_bind_int(stmt.get(), 1, bp.behavior_id);
    sqlite3_bind_text(stmt.get(), 2, bp.parameter_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.get(), 3, bp.value);
    sqlite3_step(stmt.get());

    data.params[bp.behavior_id].push_back(bp);
}

void insert_skill_raw(CdClientData& data, const std::string& db_path,
                      const SkillBehavior& sb) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    auto stmt = prepare(db.get(),
        "INSERT INTO SkillBehavior "
        "(skillID, behaviorID, imaginationcost, cooldown, cooldowngroup, "
        "inNpcEditor, skillIcon, locStatus, gate_version) "
        "VALUES (?, ?, ?, ?, ?, 0, ?, 0, ?)");
    sqlite3_bind_int(stmt.get(), 1, sb.skill_id);
    sqlite3_bind_int(stmt.get(), 2, sb.behavior_id);
    sqlite3_bind_int(stmt.get(), 3, sb.imagination_cost);
    sqlite3_bind_double(stmt.get(), 4, sb.cooldown);
    sqlite3_bind_int(stmt.get(), 5, sb.cooldown_group);
    sqlite3_bind_int(stmt.get(), 6, sb.skill_icon);
    sqlite3_bind_text(stmt.get(), 7, sb.gate_version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt.get());

    data.skills.push_back(sb);
    std::sort(data.skills.begin(), data.skills.end(),
              [](const auto& a, const auto& b) { return a.skill_id < b.skill_id; });
}

void delete_parameter(CdClientData& data, const std::string& db_path,
                      int behavior_id, const std::string& parameter_id) {
    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    auto stmt = prepare(db.get(),
        "DELETE FROM BehaviorParameter WHERE behaviorID = ? AND parameterID = ?");
    sqlite3_bind_int(stmt.get(), 1, behavior_id);
    sqlite3_bind_text(stmt.get(), 2, parameter_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt.get());

    auto it = data.params.find(behavior_id);
    if (it != data.params.end()) {
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [&](const auto& p) { return p.parameter_id == parameter_id; }),
            vec.end());
        if (vec.empty()) data.params.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Template save/load (JSON)
// ---------------------------------------------------------------------------

static void serialize_subtree(const CdClientData& data, int behavior_id,
                               std::unordered_set<int>& visited, std::string& out) {
    if (visited.count(behavior_id)) return;
    visited.insert(behavior_id);

    auto bit = data.behaviors.find(behavior_id);
    int tmpl_id = bit != data.behaviors.end() ? bit->second.template_id : 0;

    out += "{\"id\":" + std::to_string(behavior_id);
    out += ",\"template\":" + std::to_string(tmpl_id);
    out += ",\"params\":[";

    auto pit = data.params.find(behavior_id);
    if (pit != data.params.end()) {
        bool first = true;
        for (const auto& p : pit->second) {
            if (!first) out += ",";
            first = false;
            out += "{\"name\":\"" + p.parameter_id + "\",\"value\":" +
                   std::to_string(p.value) + "}";
        }
    }
    out += "],\"children\":[";

    // Recurse into child behaviors
    if (pit != data.params.end()) {
        bool first = true;
        for (const auto& p : pit->second) {
            int ref = static_cast<int>(p.value);
            if (ref > 0 && data.behaviors.count(ref) && !visited.count(ref)) {
                if (!first) out += ",";
                first = false;
                serialize_subtree(data, ref, visited, out);
            }
        }
    }
    out += "]}";
}

void save_template(const CdClientData& data, int root_behavior_id,
                   const std::string& template_path) {
    std::unordered_set<int> visited;
    std::string json;
    serialize_subtree(data, root_behavior_id, visited, json);

    std::ofstream f(template_path);
    if (!f) throw std::runtime_error("Cannot write template: " + template_path);
    f << json;
}

// Simple JSON token reader for template loading
namespace {
struct JsonReader {
    const std::string& s;
    size_t pos = 0;

    void skip_ws() { while (pos < s.size() && s[pos] <= ' ') ++pos; }
    char peek() { skip_ws(); return pos < s.size() ? s[pos] : 0; }
    void expect(char c) { skip_ws(); if (s[pos] != c) throw std::runtime_error("JSON parse error"); ++pos; }
    std::string read_string() {
        expect('"');
        size_t start = pos;
        while (pos < s.size() && s[pos] != '"') ++pos;
        std::string r = s.substr(start, pos - start);
        ++pos; // skip closing "
        return r;
    }
    double read_number() {
        skip_ws();
        size_t start = pos;
        while (pos < s.size() && (s[pos] == '-' || s[pos] == '.' || (s[pos] >= '0' && s[pos] <= '9') || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+'))
            ++pos;
        return std::stod(s.substr(start, pos - start));
    }
};

int load_node(JsonReader& jr, CdClientData& data, sqlite3* db,
              std::unordered_map<int, int>& id_map) {
    jr.expect('{');
    int orig_id = 0, tmpl_id = 0;
    std::vector<std::pair<std::string, double>> params;
    std::vector<int> child_new_ids;

    while (jr.peek() != '}') {
        if (jr.peek() == ',') { ++jr.pos; continue; }
        std::string key = jr.read_string();
        jr.expect(':');

        if (key == "id") {
            orig_id = static_cast<int>(jr.read_number());
        } else if (key == "template") {
            tmpl_id = static_cast<int>(jr.read_number());
        } else if (key == "params") {
            jr.expect('[');
            while (jr.peek() != ']') {
                if (jr.peek() == ',') { ++jr.pos; continue; }
                jr.expect('{');
                std::string pname;
                double pval = 0;
                while (jr.peek() != '}') {
                    if (jr.peek() == ',') { ++jr.pos; continue; }
                    std::string pk = jr.read_string();
                    jr.expect(':');
                    if (pk == "name") pname = jr.read_string();
                    else pval = jr.read_number();
                }
                jr.expect('}');
                params.emplace_back(pname, pval);
            }
            jr.expect(']');
        } else if (key == "children") {
            jr.expect('[');
            while (jr.peek() != ']') {
                if (jr.peek() == ',') { ++jr.pos; continue; }
                child_new_ids.push_back(load_node(jr, data, db, id_map));
            }
            jr.expect(']');
        }
    }
    jr.expect('}');

    // Create new behavior
    int new_id = next_id(db, "BehaviorTemplate", "behaviorID");
    id_map[orig_id] = new_id;

    {
        auto stmt = prepare(db,
            "INSERT INTO BehaviorTemplate (behaviorID, templateID, effectID, effectHandle) "
            "VALUES (?, ?, 0, '')");
        sqlite3_bind_int(stmt.get(), 1, new_id);
        sqlite3_bind_int(stmt.get(), 2, tmpl_id);
        sqlite3_step(stmt.get());
    }

    BehaviorTemplate bt;
    bt.behavior_id = new_id;
    bt.template_id = tmpl_id;
    data.behaviors[new_id] = bt;

    // Insert parameters, remapping behavior references to new IDs
    for (auto& [pname, pval] : params) {
        int ref = static_cast<int>(pval);
        double new_val = pval;
        if (id_map.count(ref)) new_val = static_cast<double>(id_map[ref]);

        auto stmt = prepare(db,
            "INSERT INTO BehaviorParameter (behaviorID, parameterID, value) VALUES (?, ?, ?)");
        sqlite3_bind_int(stmt.get(), 1, new_id);
        sqlite3_bind_text(stmt.get(), 2, pname.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt.get(), 3, new_val);
        sqlite3_step(stmt.get());

        BehaviorParameter bp;
        bp.behavior_id = new_id;
        bp.parameter_id = pname;
        bp.value = new_val;
        data.params[new_id].push_back(bp);
    }

    return new_id;
}
} // anonymous namespace

int load_template(CdClientData& data, const std::string& db_path,
                  const std::string& template_path) {
    std::ifstream f(template_path);
    if (!f) throw std::runtime_error("Cannot read template: " + template_path);
    std::string json((std::istreambuf_iterator<char>(f)), {});

    sqlite3* raw_db = nullptr;
    sqlite3_open(db_path.c_str(), &raw_db);
    SqliteDb db(raw_db);

    JsonReader jr{json, 0};
    std::unordered_map<int, int> id_map;
    return load_node(jr, data, db.get(), id_map);
}

// ---------------------------------------------------------------------------
// FDB loader (stub — TODO: use lu_assets FDB parser)
// ---------------------------------------------------------------------------

CdClientData load_from_fdb(const std::string& path) {
    // TODO: implement using FdbFile parser from lu_assets
    // For now, convert FDB to SQLite externally
    throw std::runtime_error("FDB loading not yet implemented — use fdb_converter to create SQLite first");
}

// ---------------------------------------------------------------------------
// Auto-detect
// ---------------------------------------------------------------------------

CdClientData load_cdclient(const std::string& path) {
    std::filesystem::path p(path);
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (ext == ".sqlite" || ext == ".db") {
        return load_from_sqlite(path);
    }
    if (ext == ".fdb") {
        return load_from_fdb(path);
    }

    // Try SQLite first (magic: "SQLite format 3\000")
    std::ifstream f(path, std::ios::binary);
    char magic[16] = {};
    f.read(magic, 16);
    if (std::string(magic, 6) == "SQLite") {
        return load_from_sqlite(path);
    }

    throw std::runtime_error("Unknown CDClient format: " + path);
}

// ---------------------------------------------------------------------------
// Behavior schema — loaded from behavior_schema.json at runtime
// ---------------------------------------------------------------------------

// Find behavior_schema.json next to the executable
static std::string find_schema_path() {
    namespace fs = std::filesystem;
    // Try next to executable
    auto exe_dir = fs::path("/proc/self/exe").parent_path();
    // On Linux /proc/self/exe is a symlink — resolve it
    std::error_code ec;
    auto resolved = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) exe_dir = resolved.parent_path();

    auto candidate = exe_dir / "behavior_schema.json";
    if (fs::exists(candidate)) return candidate.string();

    // Try current directory
    if (fs::exists("behavior_schema.json")) return "behavior_schema.json";

    // Try source directory (for development)
    candidate = fs::path(__FILE__).parent_path() / "behavior_schema.json";
    if (fs::exists(candidate)) return candidate.string();

    return {};
}

struct BehaviorSchema {
    std::unordered_map<std::string, std::vector<ParamDef>> params;
    std::unordered_map<std::string, std::vector<DynamicParamDef>> dynamic;
    std::vector<std::pair<int, std::string>> back_refs; // template_id, param_name
    mutable std::unordered_set<std::string> behavior_ref_cache; // lazily built
    bool loaded = false;
};

static BehaviorSchema& schema() {
    static BehaviorSchema s;
    if (s.loaded) return s;
    s.loaded = true;

    std::string path = find_schema_path();
    if (path.empty()) return s;

    std::ifstream f(path);
    if (!f.is_open()) return s;

    try {
        auto j = nlohmann::json::parse(f);

        // behaviors — param entries can be:
        //   {"name": "...", "type": "..."}                    — static param
        //   {"name": "...", "type": "...", "back_ref": true}  — back-reference param
        //   {"prefix": "...", "type": "..."}                  — single dynamic param
        //   {"group": [{prefix, type}, ...]}                  — paired dynamic params
        for (const auto& [name, bdef] : j["behaviors"].items()) {
            int template_id = bdef.value("template_id", 0);
            std::vector<ParamDef> params;
            std::vector<DynamicParamDef> dyn;

            if (bdef.contains("params")) {
                for (const auto& p : bdef["params"]) {
                    if (p.contains("group")) {
                        DynamicParamDef dd;
                        for (const auto& m : p["group"])
                            dd.members.push_back({m["prefix"].get<std::string>(),
                                                  m["type"].get<std::string>()});
                        dyn.push_back(std::move(dd));
                    } else if (p.contains("prefix")) {
                        DynamicParamDef dd;
                        dd.members.push_back({p["prefix"].get<std::string>(),
                                              p["type"].get<std::string>()});
                        dyn.push_back(std::move(dd));
                    } else {
                        params.push_back({p["name"].get<std::string>(),
                                          p["type"].get<std::string>()});
                        if (p.value("back_ref", false))
                            s.back_refs.emplace_back(template_id,
                                                     p["name"].get<std::string>());
                    }
                }
            }

            s.params[name] = std::move(params);
            if (!dyn.empty())
                s.dynamic[name] = std::move(dyn);
        }
    } catch (const std::exception&) {
        // Schema load failed — continue with empty schema
    }

    return s;
}

bool is_behavior_ref_param(const std::string& name) {
    const auto& s = schema();

    // Check all static schema params for type "behavior"
    if (s.behavior_ref_cache.empty()) {
        // Build cache on first call
        for (const auto& [bname, params] : s.params)
            for (const auto& pd : params)
                if (pd.type == "behavior")
                    const_cast<std::unordered_set<std::string>&>(s.behavior_ref_cache)
                        .insert(pd.name);
    }
    if (s.behavior_ref_cache.count(name)) return true;

    // Check dynamic param group members of type "behavior"
    for (const auto& [bname, dyn_list] : s.dynamic)
        for (const auto& dd : dyn_list)
            for (const auto& m : dd.members)
                if (m.type == "behavior" && name.size() > m.prefix.size() &&
                    name.substr(0, m.prefix.size()) == m.prefix)
                    return true;

    return false;
}

// Schema accessors — delegate to the JSON-loaded schema singleton.

const std::vector<ParamDef>& get_behavior_param_schema(const std::string& template_name) {
    static const std::vector<ParamDef> empty;
    const auto& s = schema();
    auto it = s.params.find(template_name);
    return it != s.params.end() ? it->second : empty;
}

const std::vector<DynamicParamDef>& get_behavior_dynamic_params(const std::string& template_name) {
    static const std::vector<DynamicParamDef> empty;
    const auto& s = schema();
    auto it = s.dynamic.find(template_name);
    return it != s.dynamic.end() ? it->second : empty;
}

bool is_back_ref_param(const std::string& param_name, int template_id) {
    const auto& s = schema();
    for (const auto& [tid, pname] : s.back_refs)
        if (tid == template_id && pname == param_name) return true;
    return false;
}

// ---------------------------------------------------------------------------

int CdClientData::skill_ref_count(int behavior_id) const {
    int count = 0;
    for (const auto& s : skills)
        if (s.behavior_id == behavior_id) ++count;
    return count;
}

int CdClientData::behavior_ref_count(int behavior_id) const {
    int count = 0;
    for (const auto& [bid, pvec] : params) {
        // Count once per parent behavior, not once per parameter
        for (const auto& p : pvec) {
            if (static_cast<int>(p.value) == behavior_id &&
                is_behavior_ref_param(p.parameter_id)) {
                ++count;
                break; // only count this parent once
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Write parameter to database
// ---------------------------------------------------------------------------

void save_parameter(CdClientData& data, const std::string& db_path,
                    int behavior_id, const std::string& parameter_id, double value) {
    // Update SQLite
    sqlite3* raw_db = nullptr;
    if (sqlite3_open(db_path.c_str(), &raw_db) != SQLITE_OK) {
        throw std::runtime_error("Cannot open database for writing");
    }
    SqliteDb db(raw_db);

    // Upsert: try UPDATE first, INSERT if no rows affected
    {
        auto stmt = prepare(db.get(),
            "UPDATE BehaviorParameter SET value = ? WHERE behaviorID = ? AND parameterID = ?");
        sqlite3_bind_double(stmt.get(), 1, value);
        sqlite3_bind_int(stmt.get(), 2, behavior_id);
        sqlite3_bind_text(stmt.get(), 3, parameter_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt.get());

        if (sqlite3_changes(db.get()) == 0) {
            auto ins = prepare(db.get(),
                "INSERT INTO BehaviorParameter (behaviorID, parameterID, value) VALUES (?, ?, ?)");
            sqlite3_bind_int(ins.get(), 1, behavior_id);
            sqlite3_bind_text(ins.get(), 2, parameter_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(ins.get(), 3, value);
            sqlite3_step(ins.get());
        }
    }

    // Update in-memory data
    auto& params = data.params[behavior_id];
    bool found = false;
    for (auto& p : params) {
        if (p.parameter_id == parameter_id) {
            p.value = value;
            found = true;
            break;
        }
    }
    if (!found) {
        BehaviorParameter bp;
        bp.behavior_id = behavior_id;
        bp.parameter_id = parameter_id;
        bp.value = value;
        params.push_back(std::move(bp));
    }
}

// ---------------------------------------------------------------------------
// Update effect on a behavior node
// ---------------------------------------------------------------------------

void save_effect(CdClientData& data, const std::string& db_path,
                 int behavior_id, int effect_id, const std::string& effect_handle) {
    sqlite3* raw_db = nullptr;
    if (sqlite3_open(db_path.c_str(), &raw_db) != SQLITE_OK) {
        throw std::runtime_error("Cannot open database for writing");
    }
    SqliteDb db(raw_db);

    auto stmt = prepare(db.get(),
        "UPDATE BehaviorTemplate SET effectID = ?, effectHandle = ? WHERE behaviorID = ?");
    sqlite3_bind_int(stmt.get(), 1, effect_id);
    sqlite3_bind_text(stmt.get(), 2, effect_handle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), 3, behavior_id);
    sqlite3_step(stmt.get());

    // Update in-memory data
    auto bit = data.behaviors.find(behavior_id);
    if (bit != data.behaviors.end()) {
        bit->second.effect_id = effect_id;
        bit->second.effect_handle = effect_handle;
    }
}

// ---------------------------------------------------------------------------
// Locale skill name loader
// ---------------------------------------------------------------------------

std::unordered_map<int, std::string> load_locale_skill_names(const std::string& locale_xml_path) {
    std::unordered_map<int, std::string> names;
    std::ifstream f(locale_xml_path);
    if (!f) return names;

    // Simple line-by-line parser — no XML library needed.
    // Looking for pairs:
    //   <phrase id="SkillBehavior_123_name">
    //     <translation locale="en_US">Some Name</translation>
    std::string line;
    int pending_skill_id = 0;
    while (std::getline(f, line)) {
        // Check for phrase id
        if (pending_skill_id == 0) {
            auto pos = line.find("SkillBehavior_");
            if (pos == std::string::npos) continue;
            auto id_start = pos + 14; // len("SkillBehavior_")
            auto id_end = line.find('_', id_start);
            if (id_end == std::string::npos) continue;
            // Check it's a _name phrase
            if (line.substr(id_end, 6) != "_name\"") continue;
            try {
                pending_skill_id = std::stoi(line.substr(id_start, id_end - id_start));
            } catch (...) { continue; }
        } else {
            // Look for en_US translation
            auto pos = line.find("locale=\"en_US\">");
            if (pos == std::string::npos) {
                // If we hit another phrase or closing tag, reset
                if (line.find("<phrase") != std::string::npos ||
                    line.find("</phrase>") != std::string::npos)
                    pending_skill_id = 0;
                continue;
            }
            auto text_start = pos + 15; // len("locale=\"en_US\">")
            auto text_end = line.find("</translation>", text_start);
            if (text_end != std::string::npos) {
                names[pending_skill_id] = line.substr(text_start, text_end - text_start);
            }
            pending_skill_id = 0;
        }
    }
    return names;
}

} // namespace skill_editor
