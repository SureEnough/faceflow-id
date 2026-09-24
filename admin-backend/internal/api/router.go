package api

import (
	"admin-backend/internal/config"
	"admin-backend/internal/object"
	"admin-backend/internal/security"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

// Server API 服务器
type Server struct {
	cfg config.Config
	db  *gorm.DB
	cip *security.Cipher
	obj object.Storage // 可为 nil（快照入库时跳过上传，仅记文本）
}

// NewServer 创建 API 服务器；obj 为可选对象存储（生产传入 MinIO/S3 实现）
func NewServer(cfg config.Config, db *gorm.DB, cip *security.Cipher, obj ...object.Storage) *Server {
	s := &Server{cfg: cfg, db: db, cip: cip}
	if len(obj) > 0 {
		s.obj = obj[0]
	}
	return s
}

// secret JWT 签名密钥
func (s *Server) secret() []byte { return []byte(s.cfg.JWTSecret) }

// Router 注册全部 REST 路由（前缀 /api/v1）
func (s *Server) Router() *gin.Engine {
	r := gin.Default()

	api := r.Group("/api/v1")
	{
		api.GET("/health", s.health)

		// 设备（注册无需自动登录；设备树/编辑等需鉴权）
		api.POST("/auth/login", s.login)
		api.POST("/auth/device/login", s.deviceLogin)
		api.POST("/devices/register", s.registerDevice)

		authed := api.Group("", s.authMiddleware(), s.auditMiddleware())
		{
			// 设备/记录/统计/查询：任意已登录 token（含设备 token）
			authed.PUT("/devices/:id", s.updateDevice)
			authed.GET("/devices", s.deviceTree)
			authed.GET("/devices/:id/config", s.deviceConfig)
			authed.PUT("/devices/:id/config", requireRole("admin", "operator"), s.updateDeviceConfig)
			authed.POST("/records/recognition/batch", s.batchRecognition)
			authed.POST("/records/verify", s.createVerify)
			authed.POST("/history/search", s.historySearch)
			// 记录查询：只读（viewer 及以上；设备 token 拒绝）
			authed.GET("/records/recognition", requireRole("admin", "operator", "viewer"), s.listRecognitionRecords)
			authed.GET("/records/verify", requireRole("admin", "operator", "viewer"), s.listVerifyRecords)

			// 导出：viewer+；审计仅 admin
			authed.GET("/export/flow.csv", requireRole("admin", "operator", "viewer"), s.exportFlowCSV)
			authed.GET("/export/customers.csv", requireRole("admin", "operator", "viewer"), s.exportCustomersCSV)

			// 全局系统配置：admin
			authed.GET("/system/config", requireRole("admin"), s.getSystemConfig)
			authed.PUT("/system/config", requireRole("admin"), s.updateSystemConfig)

			// 门店：读任意用户 token；写 admin/operator；删 admin
			authed.GET("/stores", s.listStores)
			authed.POST("/stores", requireRole("admin", "operator"), s.createStore)
			authed.PUT("/stores/:id", requireRole("admin", "operator"), s.updateStore)
			authed.DELETE("/stores/:id", requireRole("admin"), s.deleteStore)

			authed.GET("/stats/flow", s.statsFlow)
			authed.GET("/stats/staff", s.statsStaff)
			authed.GET("/stats/visits/:customer_id", s.statsVisits)
			authed.GET("/customers", s.listCustomers)
			authed.GET("/customers/features/sync", s.syncFeatures)
			authed.GET("/me", s.me)

			// 人员录入：admin / operator
			authed.POST("/customers", requireRole("admin", "operator"), s.createCustomer)
			authed.GET("/customers/import/template", requireRole("admin", "operator"), s.downloadImportTemplate)
			authed.POST("/customers/import", requireRole("admin", "operator"), s.importCustomers)
			authed.POST("/customers/:id/features", requireRole("admin", "operator"), s.appendCustomerFeature)
			// 人员档案修改/删除：admin
			authed.PUT("/customers/:id", requireRole("admin"), s.updateCustomer)
			authed.DELETE("/customers/:id", requireRole("admin"), s.deleteCustomer)

			// 令牌管理：查看（admin/operator）、吊销（admin）
			authed.GET("/tokens", requireRole("admin", "operator"), s.listTokens)
			authed.POST("/tokens/:jti/revoke", requireRole("admin"), s.revokeToken)

			// 用户管理：admin
			admin := authed.Group("", requireRole("admin"))
			{
				admin.GET("/users", s.listUsers)
				admin.GET("/audit-logs", s.listAuditLogs)
				admin.GET("/export/audit.csv", s.exportAuditCSV)
				admin.POST("/users", s.createUser)
				admin.PUT("/users/:id", s.updateUser)
				admin.DELETE("/users/:id", s.deleteUser)
			}
		}
	}

	return r
}

func (s *Server) health(c *gin.Context) {
	OK(c, gin.H{"status": "up", "time": nowUTC()})
}