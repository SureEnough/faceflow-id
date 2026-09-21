package api

import (
	"admin-backend/internal/config"
	"admin-backend/internal/security"

	"github.com/gin-gonic/gin"
	"gorm.io/gorm"
)

// Server API 服务器
type Server struct {
	cfg config.Config
	db  *gorm.DB
	cip *security.Cipher
}

func NewServer(cfg config.Config, db *gorm.DB, cip *security.Cipher) *Server {
	return &Server{cfg: cfg, db: db, cip: cip}
}

// secret JWT 签名密钥
func (s *Server) secret() []byte { return []byte(s.cfg.JWTSecret) }

// Router 注册全部 REST 路由（前缀 /api/v1）
func (s *Server) Router() *gin.Engine {
	r := gin.Default()

	api := r.Group("/api/v1")
	{
		api.GET("/health", s.health)

		// 设备（注册/心跳无需自动登录；设备树需鉴权）
		api.POST("/auth/login", s.login)
		api.POST("/devices/register", s.registerDevice)
		api.POST("/devices/:id/heartbeat", s.deviceHeartbeat)

		authed := api.Group("", authMiddlewareWithSecret(s.secret()), s.auditMiddleware())
		{
			// 设备/记录/统计/查询：任意已登录 token（含设备 token）
			authed.GET("/devices", s.deviceTree)
			authed.GET("/devices/:id/config", s.deviceConfig)
			authed.POST("/records/recognition/batch", s.batchRecognition)
			authed.POST("/records/verify", s.createVerify)
			authed.POST("/history/search", s.historySearch)
			authed.GET("/stats/flow", s.statsFlow)
			authed.GET("/stats/staff", s.statsStaff)
			authed.GET("/stats/visits/:customer_id", s.statsVisits)
			authed.GET("/customers", s.listCustomers)
			authed.GET("/customers/features/sync", s.syncFeatures)
			authed.GET("/me", s.me)

			// 人员录入：admin / operator
			authed.POST("/customers", requireRole("admin", "operator"), s.createCustomer)
			authed.POST("/customers/:id/features", requireRole("admin", "operator"), s.appendCustomerFeature)
			// 人员档案修改/删除：admin
			authed.PUT("/customers/:id", requireRole("admin"), s.updateCustomer)
			authed.DELETE("/customers/:id", requireRole("admin"), s.deleteCustomer)

			// 用户管理：admin
			admin := authed.Group("", requireRole("admin"))
			{
				admin.GET("/users", s.listUsers)
				admin.GET("/audit-logs", s.listAuditLogs)
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