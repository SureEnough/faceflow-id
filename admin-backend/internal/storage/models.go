package storage

import (
	"gorm.io/gorm"
)

// 设备类型枚举（与架构设计文档 5.2.4 一致）
const (
	DeviceTypeEdgeBox    = 1 // 边缘盒子（主设备）
	DeviceTypeEnrollPC   = 2 // 录入电脑端（主设备）
	DeviceTypeRTSPCam    = 3 // RTSP 摄像头（子设备，父=边缘盒子）
	DeviceTypeUSBCam     = 4 // USB 摄像头（子设备，父=录入电脑端）
	DeviceTypeIDCardRead = 5 // 身份证读卡器（子设备，父=录入电脑端）
)

// 人员类型
const (
	PersonTypeCustomer = 0 // 顾客
	PersonTypeStaff    = 1 // 内部人员（STAFF）
)

// 人员状态
const (
	PersonStatusNormal   = 0
	PersonStatusBlack    = 1
	PersonStatusDisabled = 2
	PersonStatusLeft     = 3 // 离职
)

// 识别记录方向
const (
	DirectionIn  = 0
	DirectionOut = 1
)

// Store 门店
type Store struct {
	ID        uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	Name      string    `gorm:"size:64;not null" json:"name"`
	Address   string    `gorm:"size:255" json:"address"`
	Status    int8      `gorm:"not null;default:1" json:"status"`
	CreatedAt int64 `json:"created_at"` // Unix 秒
	UpdatedAt int64 `json:"updated_at"` // Unix 秒
}

// Device 设备（5 类，父子层级：RTSP摄像头→边缘盒子；USB摄像头/读卡器→录入电脑端）
type Device struct {
	ID            uint64     `gorm:"primaryKey;autoIncrement" json:"id"`
	DeviceType    int8       `gorm:"not null;index" json:"device_type"`
	ParentID      *uint64    `gorm:"index" json:"parent_id"` // 主设备为 NULL
	DeviceKey     string     `gorm:"size:64" json:"device_key"`
	Name          string     `gorm:"size:64;not null" json:"name"`
	StoreID       uint64     `gorm:"not null;index" json:"store_id"`
	Status        int8       `gorm:"not null;default:0" json:"status"` // 0 离线 / 1 在线
	PSKHash       string     `gorm:"size:128" json:"-"`
	LastHeartbeat int64 `json:"last_heartbeat"` // Unix 秒，0=从未心跳
	ConfigJSON    string     `gorm:"type:text" json:"config_json,omitempty"`
	CreatedAt     int64  `json:"created_at"` // Unix 秒
	UpdatedAt     int64  `json:"updated_at"` // Unix 秒

	Children []*Device `gorm:"-" json:"children,omitempty"` // 设备树子节点
}

// Customer 人员档案（顾客 / 内部人员）
type Customer struct {
	ID            uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	PersonType    int8      `gorm:"not null;default:0;index" json:"person_type"`
	NameEnc       []byte    `gorm:"type:blob" json:"-"`           // AES-GCM 密文
	IDCardNoEnc   []byte    `gorm:"type:blob;uniqueIndex" json:"-"` // 仅顾客
	StaffNo       *string   `gorm:"size:32;uniqueIndex" json:"staff_no,omitempty"` // 仅内部人员，顾客为 NULL
	Department    string    `gorm:"size:64" json:"department,omitempty"`
	Gender        int8      `json:"gender,omitempty"`
	BirthDate     int64      `json:"birth_date,omitempty"` // Unix 秒（当日 00:00Z）
	AddressEnc    []byte    `gorm:"type:blob" json:"-"`
	IDPhotoPath   string    `gorm:"size:255" json:"id_photo_path,omitempty"`
	LivePhotoPath string    `gorm:"size:255" json:"live_photo_path,omitempty"`
	FaceFeature   []byte    `gorm:"type:blob" json:"-"` // 512*float32 主特征
	Status        int8      `gorm:"not null;default:0" json:"status"`
	Version       uint64    `gorm:"not null;default:1" json:"version"` // 增量同步游标
	CreatedBy     string    `gorm:"size:64" json:"created_by,omitempty"`
	CreatedAt int64 `json:"created_at"` // Unix 秒
	UpdatedAt int64 `json:"updated_at"` // Unix 秒

	// 请求/响应辅助字段（不入库）
	Name       string  `gorm:"-" json:"name"`
	IDCardNo   string  `gorm:"-" json:"id_card_no,omitempty"`
	Address    string  `gorm:"-" json:"address,omitempty"`
	History    *Visits `gorm:"-" json:"history,omitempty"` // 录入时返回历史来访
}

// CustomerFeature 顾客附加特征（多特征支持）
type CustomerFeature struct {
	ID          uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	CustomerID  uint64    `gorm:"not null;index" json:"customer_id"`
	FaceFeature []byte    `gorm:"type:blob" json:"-"`
	Source      int8      `gorm:"not null;default:0" json:"source"` // 0 录入 / 1 现场补采
	CreatedAt int64 `json:"created_at"` // Unix 秒
}

// RecognitionLog 识别记录 / 匿名轨迹
type RecognitionLog struct {
	ID          uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	DeviceID    uint64    `gorm:"not null" json:"device_id"`
	TrackID     string    `gorm:"size:64;not null" json:"track_id"`
	CustomerID  *uint64   `gorm:"index" json:"customer_id,omitempty"` // 命中回填，匿名为 NULL
	PersonType  int8      `gorm:"not null;default:0;index" json:"person_type"`
	FaceFeature []byte    `gorm:"type:blob" json:"-"`
	Snapshot    string    `gorm:"type:text" json:"snapshot,omitempty"`       // base64 图片或对象存储 key
	SnapshotMime string   `gorm:"size:32" json:"snapshot_mime,omitempty"`   // image/jpeg / image/bmp
	Similarity  float32   `json:"similarity"`
	Direction   int8      `gorm:"not null;default:0" json:"direction"`
	CameraID    string    `gorm:"size:32" json:"camera_id"`
	CreatedAt   int64     `gorm:"not null" json:"created_at"` // Unix 秒

	// 幂等唯一键 (device_id, track_id, camera_id, created_at) 建联合唯一索引
}

// TableName 指定表名
func (RecognitionLog) TableName() string { return "recognition_logs" }

// VerifyRecord 人证核验记录
type VerifyRecord struct {
	ID            uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	CustomerID    uint64    `gorm:"not null;index" json:"customer_id"`
	IDCardNoEnc   []byte    `gorm:"type:blob" json:"-"`
	VerifyResult  int8      `gorm:"not null" json:"verify_result"` // 0 待定 / 1 通过 / 2 不通过
	Similarity    float32   `json:"similarity"`
	LivenessScore float32   `json:"liveness_score"`
	LivePhotoPath string    `gorm:"size:255" json:"live_photo_path,omitempty"`
	DeviceID      uint64    `json:"device_id"`
	Operator      string    `gorm:"size:64" json:"operator,omitempty"`
	CreatedAt int64 `json:"created_at"` // Unix 秒
}

// Visits 来访统计（物化或查询结果；时间字段为 Unix 秒）
type Visits struct {
	CustomerID   uint64 `gorm:"primaryKey" json:"customer_id"`
	TotalVisits  int64  `json:"total_visits"`
	VisitDays    int64  `json:"visit_days"`
	FirstVisitAt int64  `json:"first_visit_at"` // Unix 秒，0=无
	LastVisitAt  int64  `json:"last_visit_at"`  // Unix 秒，0=无
	UpdatedAt    int64  `json:"updated_at"`
}

// VisitStatsRow 客流/员工统计聚合行（列名 flow_in/flow_out 避开 SQL 保留字 in/out）
type VisitStatsRow struct {
	Bucket string `json:"bucket"`
	In     int64  `json:"in" gorm:"column:flow_in"`
	Out    int64  `json:"out" gorm:"column:flow_out"`
}

// User 后台账号
type User struct {
	ID           uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	Username     string    `gorm:"size:64;uniqueIndex;not null" json:"username"`
	PasswordHash string    `gorm:"size:128;not null" json:"-"`
	Role         int8      `gorm:"not null" json:"role"` // 0 管理员 / 1 操作员 / 2 只读
	Status       int8      `gorm:"not null;default:1" json:"status"`
	CreatedAt    int64     `json:"created_at"` // Unix 秒
}

// AuditLog 操作审计日志
type AuditLog struct {
	ID         uint64 `gorm:"primaryKey;autoIncrement" json:"id"`
	UserID     int64  `gorm:"index" json:"user_id"`      // 触发者（后台用户）；设备写入时可为 0
	Username   string `gorm:"size:64" json:"username"`
	Action     string `gorm:"size:64;not null" json:"action"` // 例如 POST /api/v1/customers
	TargetType string `gorm:"size:32" json:"target_type"`
	TargetID   int64  `json:"target_id"`
	Detail     string `gorm:"type:text" json:"detail,omitempty"`
	IP         string `gorm:"size:64" json:"ip"`
	CreatedAt  int64  `json:"created_at"` // Unix 秒
}

// TokenRecord 令牌记录（签发落库，用于吊销与管理；JWT 本身无状态）
type TokenRecord struct {
	Jti         string `gorm:"primaryKey;size:64" json:"jti"`
	SubjectKind string `gorm:"size:16;index;not null" json:"subject_kind"` // user / device
	SubjectID   int64  `gorm:"index;not null" json:"subject_id"`
	Role        string `gorm:"size:16;not null" json:"role"` // admin / operator / viewer / device
	IssuedAt    int64  `gorm:"not null" json:"issued_at"`    // Unix 秒
	ExpiresAt   int64  `gorm:"index;not null" json:"expires_at"` // Unix 秒
	RevokedAt   *int64 `json:"revoked_at"`                        // nil = 有效
	RevokedBy   int64  `gorm:"not null;default:0" json:"revoked_by"` // 操作者 user id；0 = 系统/未知
	LastUsedAt  int64  `gorm:"not null;default:0" json:"last_used_at"` // Unix 秒
}

// TableName 指定表名（避免与系统表冲突）
func (TokenRecord) TableName() string { return "tokens" }

// 自动迁移（开发期使用；生产建议用 migrations/schema.sql）
func AutoMigrate(db *gorm.DB) error {
	return db.AutoMigrate(
		&Store{}, &Device{}, &Customer{}, &CustomerFeature{},
		&RecognitionLog{}, &VerifyRecord{}, &Visits{}, &User{}, &AuditLog{},
		&TokenRecord{},
	)
}