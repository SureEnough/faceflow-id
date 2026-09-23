package api

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"time"
)

// saveImage 将 base64 图片转存到对象存储，返回对象 key。
// 规则：
//   - 对象存储未开启（s.obj == nil）或解码失败 → 原样返回输入（开发模式文本记录）；
//   - 转存成功 → 返回对象 key（前端可经 object.URL / 对象下载接口取图）。
func (s *Server) saveImage(ctx context.Context, b64, folder string) string {
	if s.obj == nil || b64 == "" {
		return b64
	}
	b, err := base64.StdEncoding.DecodeString(b64)
	if err != nil || len(b) == 0 {
		return b64
	}
	ct := "image/jpeg"
	if len(b) > 2 && b[0] == 'B' && b[1] == 'M' {
		ct = "image/bmp"
	}
	key := fmt.Sprintf("%s/%d/%s.jpg", folder, time.Now().Unix(), randHex(8))
	if err := s.obj.Put(ctx, key, bytes.NewReader(b), ct); err != nil {
		s.logf("save image failed: %v", err)
		return b64
	}
	return key
}

// randHex 生成 n 字节随机 hex（对象 key 后缀）
func randHex(n int) string {
	b := make([]byte, n)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}