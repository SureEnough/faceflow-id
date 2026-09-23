-- ============================================================
-- FaceFlow · 智脸客流人证系统 - 管理后台 DDL
-- 目标数据库：MySQL 8.x（开发期可改用 SQLite + AutoMigrate）
-- 说明：敏感字段以 AES-GCM 密文（VARBINARY）存储，应用层加解密
-- ============================================================

-- 门店
CREATE TABLE IF NOT EXISTS stores (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  name          VARCHAR(64)  NOT NULL,
  address       VARCHAR(255) DEFAULT '',
  status        TINYINT      NOT NULL DEFAULT 1 COMMENT '1 营业 / 0 停用',
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备（5 类，父子层级：RTSP摄像头→边缘盒子；USB摄像头/读卡器→录入电脑端）
CREATE TABLE IF NOT EXISTS devices (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_type   TINYINT      NOT NULL COMMENT '1 边缘盒子 / 2 录入电脑端 / 3 RTSP摄像头 / 4 USB摄像头 / 5 身份证读卡器',
  parent_id     BIGINT UNSIGNED DEFAULT NULL COMMENT '父级设备 ID，主设备为 NULL',
  device_key    VARCHAR(64)  DEFAULT '' COMMENT 'camera_id / usb通道 / 读卡器编号',
  name          VARCHAR(64)  NOT NULL,
  store_id      BIGINT UNSIGNED NOT NULL,
  status        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 离线 / 1 在线',
  psk_hash      VARCHAR(128) DEFAULT NULL COMMENT '仅主设备',
  last_seen_at    BIGINT      DEFAULT 0,               -- Unix 秒（最后在线时间）
  cpu             DOUBLE      DEFAULT 0,               -- 最近上报 CPU 使用率 %
  mem             DOUBLE      DEFAULT 0,               -- 最近上报内存使用率 %
  disk            DOUBLE      DEFAULT 0,               -- 最近上报磁盘使用率 %
  fps             DOUBLE      DEFAULT 0,               -- 最近上报处理帧率
  config_json   JSON         DEFAULT NULL,
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL,
  KEY idx_store (store_id),
  KEY idx_type (device_type),
  KEY idx_parent (parent_id),
  CONSTRAINT fk_dev_store FOREIGN KEY (store_id) REFERENCES stores(id),
  CONSTRAINT fk_dev_parent FOREIGN KEY (parent_id) REFERENCES devices(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 人员档案（person_type=0 顾客 / =1 内部人员）
CREATE TABLE IF NOT EXISTS customers (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  person_type   TINYINT      NOT NULL DEFAULT 0 COMMENT '0 顾客 / 1 内部人员(STAFF)',
  name_enc      VARBINARY(256)  NOT NULL COMMENT 'AES-GCM 密文',
  id_card_no_enc VARBINARY(128) DEFAULT NULL COMMENT 'AES-GCM 密文，仅顾客，唯一',
  staff_no      VARCHAR(32)  DEFAULT NULL COMMENT '工号，仅内部人员，唯一',
  department    VARCHAR(64)  DEFAULT '' COMMENT '部门/岗位，仅内部人员',
  gender        TINYINT      DEFAULT NULL,
  birth_date    BIGINT       DEFAULT 0,                -- Unix 秒
  address_enc   VARBINARY(1024) DEFAULT NULL,
  id_photo_path VARCHAR(255) DEFAULT '',
  live_photo_path VARCHAR(255) DEFAULT '',
  face_feature  BLOB         DEFAULT NULL COMMENT '主特征 512*float32',
  status        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 正常 / 1 黑名单 / 2 注销 / 3 离职',
  version       BIGINT       NOT NULL DEFAULT 1 COMMENT '特征版本（增量同步游标）',
  created_by    VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒（应用层写入）
  updated_at    BIGINT       NOT NULL,
  UNIQUE KEY uk_card (id_card_no_enc),
  UNIQUE KEY uk_staff (staff_no),
  KEY idx_type (person_type),
  KEY idx_status (status),
  KEY idx_version (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 人员附加特征（多特征支持）
CREATE TABLE IF NOT EXISTS customer_features (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  customer_id   BIGINT UNSIGNED NOT NULL,
  face_feature  BLOB         NOT NULL,
  source        TINYINT      NOT NULL DEFAULT 0 COMMENT '0 录入 / 1 现场补采',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_customer (customer_id),
  CONSTRAINT fk_cf_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 识别记录 / 匿名轨迹（边缘盒上报，幂等键含 camera_id）
CREATE TABLE IF NOT EXISTS recognition_logs (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  device_id     BIGINT UNSIGNED NOT NULL,
  track_id      VARCHAR(64)  NOT NULL,
  customer_id   BIGINT UNSIGNED DEFAULT NULL COMMENT '命中回填，匿名为 NULL',
  person_type   TINYINT      NOT NULL DEFAULT 0 COMMENT '0 顾客/匿名 / 1 内部人员(STAFF)',
  face_feature  BLOB         DEFAULT NULL,
  snapshot_path VARCHAR(255) DEFAULT '',
  similarity    FLOAT        DEFAULT 0,
  direction     TINYINT      DEFAULT 0 COMMENT '0 进 / 1 出',
  camera_id     VARCHAR(32)  DEFAULT '' COMMENT '多路摄像头标识（store+device+camera_no）',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  UNIQUE KEY uk_dedup (device_id, track_id, camera_id, created_at),
  KEY idx_time (created_at),
  KEY idx_customer (customer_id),
  KEY idx_type (person_type),
  KEY idx_device_time (device_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 人证核验记录
CREATE TABLE IF NOT EXISTS verify_records (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  customer_id   BIGINT UNSIGNED NOT NULL,
  id_card_no_enc VARBINARY(128) DEFAULT NULL,
  verify_result TINYINT      NOT NULL COMMENT '0 待定 / 1 通过 / 2 不通过',
  similarity    FLOAT        DEFAULT 0,
  liveness_score FLOAT       DEFAULT 0,
  live_photo_path VARCHAR(255) DEFAULT '',
  device_id     BIGINT UNSIGNED DEFAULT NULL,
  operator      VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_time (created_at),
  KEY idx_customer (customer_id),
  CONSTRAINT fk_vr_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 来访统计（物化，回查后更新；也可实时计算不落库）
CREATE TABLE IF NOT EXISTS visit_stats (
  customer_id   BIGINT UNSIGNED PRIMARY KEY,
  total_visits  INT          NOT NULL DEFAULT 0,
  visit_days    INT          NOT NULL DEFAULT 0,
  first_visit_at BIGINT      DEFAULT 0,                -- Unix 秒
  last_visit_at  BIGINT      DEFAULT 0,                -- Unix 秒
  updated_at    BIGINT       NOT NULL,                -- Unix 秒
  CONSTRAINT fk_vs_customer FOREIGN KEY (customer_id) REFERENCES customers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 后台账号
CREATE TABLE IF NOT EXISTS users (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  username      VARCHAR(64)  NOT NULL UNIQUE,
  password_hash VARCHAR(128) NOT NULL,
  role          TINYINT      NOT NULL DEFAULT 1 COMMENT '0 管理员 / 1 操作员 / 2 只读',
  status        TINYINT      NOT NULL DEFAULT 1,
  created_at    BIGINT       NOT NULL                -- Unix 秒
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 操作审计日志
CREATE TABLE IF NOT EXISTS audit_logs (
  id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  user_id       BIGINT UNSIGNED DEFAULT NULL,
  action        VARCHAR(64)  NOT NULL,
  target_type   VARCHAR(32)  DEFAULT '',
  target_id     BIGINT       DEFAULT NULL,
  detail        JSON         DEFAULT NULL,
  ip            VARCHAR(64)  DEFAULT '',
  created_at    BIGINT       NOT NULL,                -- Unix 秒
  KEY idx_time (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;