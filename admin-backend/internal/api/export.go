package api

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// csvEscape CSV 字段转义（逗号/引号/换行 → 双引号包裹）
func csvEscape(s string) string {
	if strings.ContainsAny(s, ",\"\n\r") {
		return `"` + strings.ReplaceAll(s, `"`, `""`) + `"`
	}
	return s
}

func writeCSV(c *gin.Context, filename string, buf *bytes.Buffer) {
	c.Header("Content-Type", "text/csv; charset=utf-8")
	c.Header("Content-Disposition", `attachment; filename="`+filename+`"`)
	c.String(http.StatusOK, "\xEF\xBB\xBF"+buf.String()) // UTF-8 BOM 前置（Excel 识别中文）
}

// GET /export/flow.csv 客流统计导出（参数同 /stats/flow）
func (s *Server) exportFlowCSV(c *gin.Context) {
	p, err := parseFlowParams(c)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	rows, _, _, _, err := s.flowData(c, p)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	var buf bytes.Buffer
	if p.groupByCamera {
		buf.WriteString("camera_id,bucket,in,out\n")
		for _, r := range rows {
			fmt.Fprintf(&buf, "%s,%s,%d,%d\n", csvEscape(r.CameraID), csvEscape(r.Bucket), r.In, r.Out)
		}
	} else {
		buf.WriteString("bucket,in,out\n")
		for _, r := range rows {
			fmt.Fprintf(&buf, "%s,%d,%d\n", csvEscape(r.Bucket), r.In, r.Out)
		}
	}
	writeCSV(c, "flow.csv", &buf)
}

// GET /export/customers.csv 人员库导出
// 过滤：person_type / status / store_id（可空）；最多 1 万条
func (s *Server) exportCustomersCSV(c *gin.Context) {
	q := s.db.Model(&storage.Customer{})
	if v := c.Query("person_type"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("person_type = ?", n)
		}
	}
	if v := c.Query("status"); v != "" {
		if n, err := strconv.ParseInt(v, 10, 64); err == nil {
			q = q.Where("status = ?", n)
		}
	}
	if v := c.Query("store_id"); v != "" {
		if n, err := strconv.ParseUint(v, 10, 64); err == nil && n > 0 {
			q = q.Where("store_id = ?", n)
		}
	}
	var rows []storage.Customer
	if err := q.Order("id ASC").Limit(10000).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	for i := range rows {
		s.decryptCustomer(&rows[i])
	}
	// 门店名称映射（一次查询）
	storeNames := s.storeNameMap(c.Request.Context())

	var buf bytes.Buffer
	buf.WriteString("id,person_type,name,id_card_no,staff_no,department,gender,birth_date,store_id,store_name,status,created_at\n")
	for _, r := range rows {
		birth := ""
		if r.BirthDate > 0 {
			birth = time.Unix(r.BirthDate, 0).UTC().Format("2006-01-02")
		}
		fmt.Fprintf(&buf, "%d,%d,%s,%s,%s,%s,%d,%s,%d,%s,%d,%d\n",
			r.ID, r.PersonType, csvEscape(r.Name), csvEscape(r.IDCardNo), csvEscape(derefStr(r.StaffNo)),
			csvEscape(r.Department), r.Gender, birth, r.StoreID, csvEscape(storeNames[r.StoreID]), r.Status, r.CreatedAt)
	}
	writeCSV(c, "customers.csv", &buf)
}

// storeNameMap 门店 id -> 名称（空名用空串，调用方自行兜底）
func (s *Server) storeNameMap(ctx context.Context) map[uint64]string {
	m := make(map[uint64]string)
	var rows []storage.Store
	if err := s.db.WithContext(ctx).Select("id, name").Find(&rows).Error; err != nil {
		return m
	}
	for _, r := range rows {
		m[r.ID] = r.Name
	}
	return m
}

// GET /export/audit.csv 审计日志导出（admin；最多 1 万条）
func (s *Server) exportAuditCSV(c *gin.Context) {
	var rows []storage.AuditLog
	if err := s.db.Order("id DESC").Limit(10000).Find(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	var buf bytes.Buffer
	buf.WriteString("id,username,action,target_type,target_id,detail,ip,created_at\n")
	for _, r := range rows {
		fmt.Fprintf(&buf, "%d,%s,%s,%s,%d,%s,%s,%s\n",
			r.ID, csvEscape(r.Username), csvEscape(r.Action), csvEscape(r.TargetType),
			r.TargetID, csvEscape(r.Detail), csvEscape(r.IP), formatTime(r.CreatedAt))
	}
	writeCSV(c, "audit.csv", &buf)
}

func derefStr(p *string) string {
	if p == nil {
		return ""
	}
	return *p
}