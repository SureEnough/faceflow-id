package main

import (
	"log"

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