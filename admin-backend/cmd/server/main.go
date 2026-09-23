package main

import (
	"log"
	"time"

	"admin-backend/internal/api"
	"admin-backend/internal/config"
	"admin-backend/internal/object"
	"admin-backend/internal/security"
	"admin-backend/internal/storage"
)

func main() {
	cfg := config.Load()

	cip, err := security.NewCipher(cfg.AESKey)
	if err != nil {
		log.Fatalf("cipher init failed: %v", err)
	}

	db, err := storage.Open(cfg.DBDSN)
	if err != nil {
		log.Fatalf("db open failed: %v", err)
	}
	if err := storage.AutoMigrate(db); err != nil {
		log.Fatalf("migrate failed: %v", err)
	}

	var obj object.Storage
	if cfg.ObjectRoot != "" {
		obj = object.NewLocal(cfg.ObjectRoot, cfg.ObjectPublicURL)
		log.Printf("object storage: local root=%s public=%q", cfg.ObjectRoot, cfg.ObjectPublicURL)
	}
	// 定时清理过期识别/核验记录（保留期 RETENTION_DAYS，默认 365 天；每天检查一次）
	if cfg.RetentionDay > 0 {
		go func() {
			ticker := time.NewTicker(24 * time.Hour)
			defer ticker.Stop()
			for {
				if n, err := storage.CleanupExpired(db, cfg.RetentionDay); err != nil {
					log.Printf("cleanup expired failed: %v", err)
				} else if n > 0 {
					log.Printf("cleanup expired records: %d removed", n)
				}
				<-ticker.C
			}
		}()
		log.Printf("retention cleanup enabled: %d days", cfg.RetentionDay)
	}

	srv := api.NewServer(cfg, db, cip, obj)
	if err := srv.EnsureAdmin(); err != nil {
		log.Fatalf("ensure admin failed: %v", err)
	}
	r := srv.Router()

	log.Printf("admin-backend listening on %s (aes=%v)", cfg.HTTPAddr, cip.Enabled())
	if err := r.Run(cfg.HTTPAddr); err != nil {
		log.Fatalf("server exit: %v", err)
	}
}