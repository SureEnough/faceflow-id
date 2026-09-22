#include "store/recognition_store.h"

#include <algorithm>
#include <ctime>
#include <deque>
#include <mutex>

namespace eb {

// 内存实现（无 SQLite 依赖：开发自测/CI）
class MemStore : public RecognitionStore {
 public:
  bool Insert(const Recognition& rec) override {
    std::lock_guard<std::mutex> lk(mu_);
    items_.push_back(StoredRecognition{rec, 0});
    return true;
  }

  std::vector<StoredRecognition> Pending(int limit) override {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<StoredRecognition> out;
    for (auto& it : items_) {
      if (it.sync_status == 0) {
        out.push_back(it);
        if (static_cast<int>(out.size()) >= limit) break;
      }
    }
    return out;
  }

  void MarkConfirmed(const Recognition& rec) override {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& it : items_) {
      if (it.rec.track_id == rec.track_id && it.rec.camera_id == rec.camera_id &&
          it.rec.created_at == rec.created_at) {
        it.sync_status = 1;
        return;
      }
    }
  }

  int64_t Cleanup(int64_t now_unix, int retention_days) override {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t before = static_cast<int64_t>(items_.size());
    int64_t cutoff = now_unix - static_cast<int64_t>(retention_days) * 24 * 3600;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [cutoff](const StoredRecognition& it) { return it.rec.created_at < cutoff; }),
                 items_.end());
    return before - static_cast<int64_t>(items_.size());
  }

 private:
  std::deque<StoredRecognition> items_;
  mutable std::mutex mu_;
};

#if defined(HAVE_SQLITE3)
// SQLite 实现（真实设备）：建表 recognition_logs，唯一键 (track_id, camera_id, created_at)
// 与后台幂等键 device_id+track_id+camera_id+created_at 对齐（device_id 单设备恒定）。
#include <sqlite3.h>

#include <cstdio>

class SQLiteStore : public RecognitionStore {
 public:
  explicit SQLiteStore(const std::string& path) : path_(path) { Open(); }

  ~SQLiteStore() override {
    if (db_) sqlite3_close(db_);
  }

  bool Insert(const Recognition& rec) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO recognition_logs"
        "(track_id, customer_id, person_type, similarity, direction, camera_id, created_at,"
        " snapshot_b64, snapshot_mime, sync_status)"
        " VALUES (?,?,?,?,?,?,?,?,?,0);";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
      LOG_ERROR("sqlite prepare insert failed: %s", sqlite3_errmsg(db_));
      return false;
    }
    sqlite3_bind_text(st, 1, rec.track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, rec.customer_id);
    sqlite3_bind_int(st, 3, rec.person_type);
    sqlite3_bind_double(st, 4, rec.similarity);
    sqlite3_bind_int(st, 5, rec.direction);
    sqlite3_bind_text(st, 6, rec.camera_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 7, rec.created_at);
    sqlite3_bind_text(st, 8, rec.snapshot_b64.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, rec.snapshot_mime.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
      LOG_ERROR("sqlite insert failed: %s", sqlite3_errmsg(db_));
      return false;
    }
    return true;
  }

  std::vector<StoredRecognition> Pending(int limit) override {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<StoredRecognition> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "SELECT track_id, customer_id, person_type, similarity, direction, camera_id, created_at,"
        " snapshot_b64, snapshot_mime"
        " FROM recognition_logs WHERE sync_status=0 ORDER BY created_at ASC LIMIT ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
      LOG_ERROR("sqlite prepare pending failed: %s", sqlite3_errmsg(db_));
      return out;
    }
    sqlite3_bind_int(st, 1, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
      StoredRecognition sr;
      sr.rec.track_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
      sr.rec.customer_id = sqlite3_column_int64(st, 1);
      sr.rec.person_type = static_cast<PersonType>(sqlite3_column_int(st, 2));
      sr.rec.similarity = static_cast<float>(sqlite3_column_double(st, 3));
      sr.rec.direction = sqlite3_column_int(st, 4);
      sr.rec.camera_id = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
      sr.rec.created_at = sqlite3_column_int64(st, 6);
      sr.rec.snapshot_b64 = reinterpret_cast<const char*>(sqlite3_column_text(st, 7));
      sr.rec.snapshot_mime = reinterpret_cast<const char*>(sqlite3_column_text(st, 8));
      sr.sync_status = 0;
      out.push_back(std::move(sr));
    }
    sqlite3_finalize(st);
    return out;
  }

  void MarkConfirmed(const Recognition& rec) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) return;
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "UPDATE recognition_logs SET sync_status=1"
        " WHERE track_id=? AND camera_id=? AND created_at=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
      LOG_ERROR("sqlite prepare mark failed: %s", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_text(st, 1, rec.track_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, rec.camera_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, rec.created_at);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }

  int64_t Cleanup(int64_t now_unix, int retention_days) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (!db_) return 0;
    int64_t cutoff = now_unix - static_cast<int64_t>(retention_days) * 24 * 3600;
    sqlite3_stmt* st = nullptr;
    const char* sql = "DELETE FROM recognition_logs WHERE created_at < ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
      LOG_ERROR("sqlite prepare cleanup failed: %s", sqlite3_errmsg(db_));
      return 0;
    }
    sqlite3_bind_int64(st, 1, cutoff);
    int rc = sqlite3_step(st);
    int64_t changes = sqlite3_changes64(db_);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? changes : 0;
  }

 private:
  void Open() {
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
      LOG_ERROR("sqlite open failed: %s", db_ ? sqlite3_errmsg(db_) : "unknown");
      if (db_) { sqlite3_close(db_); db_ = nullptr; }
      return;
    }
    // 生产库应开启 WAL 减少并发锁
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    const char* schema =
        "CREATE TABLE IF NOT EXISTS recognition_logs ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " track_id TEXT NOT NULL,"
        " customer_id INTEGER NOT NULL DEFAULT -1,"
        " person_type INTEGER NOT NULL DEFAULT 0,"
        " similarity REAL NOT NULL DEFAULT 0,"
        " direction INTEGER NOT NULL DEFAULT -1,"
        " camera_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " snapshot_b64 TEXT NOT NULL DEFAULT '',"
        " snapshot_mime VARCHAR(32) NOT NULL DEFAULT '',"
        " sync_status INTEGER NOT NULL DEFAULT 0,"
        " UNIQUE (track_id, camera_id, created_at));"
        "CREATE INDEX IF NOT EXISTS idx_recognition_logs_sync"
        " ON recognition_logs(sync_status, created_at);";
    char* err = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err) != SQLITE_OK) {
      LOG_ERROR("sqlite schema failed: %s", err ? err : "unknown");
      sqlite3_free(err);
    }
    // 旧库迁移：缺失的 snapshot 列补上（ALTER TABLE ADD COLUMN 幂等）
    {
      const char* cols[2][2] = {{"snapshot_b64", "TEXT NOT NULL DEFAULT ''"},
                                {"snapshot_mime", "VARCHAR(32) NOT NULL DEFAULT ''"}};
      for (auto& c : cols) {
        sqlite3_stmt* q = nullptr;
        sqlite3_prepare_v2(db_, "PRAGMA table_info(recognition_logs);", -1, &q, nullptr);
        bool has = false;
        while (sqlite3_step(q) == SQLITE_ROW) {
          const char* nm = reinterpret_cast<const char*>(sqlite3_column_text(q, 1));
          if (nm && c[0] == nm) has = true;
        }
        sqlite3_finalize(q);
        if (!has) {
          std::string alter = "ALTER TABLE recognition_logs ADD COLUMN ";
          alter += c[0]; alter += " "; alter += c[1];
          sqlite3_exec(db_, alter.c_str(), nullptr, nullptr, nullptr);
        }
      }
    }
  }

  std::string path_;
  sqlite3* db_ = nullptr;
  std::mutex mu_;
};
#endif

RecognitionStore* CreateRecognitionStore() {
#if defined(HAVE_SQLITE3)
  return new SQLiteStore("edge_box.db");
#else
  return new MemStore();
#endif
}

RecognitionStore* CreateMemStore() {
  return new MemStore();
}

}  // namespace eb