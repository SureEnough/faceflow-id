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
	"github.com/xuri/excelize/v2"
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

// loadCustomersForExport 按导出筛选（person_type/status/store_id，可空）查询人员库，
// 解密敏感字段并返回门店名称映射；最多 1 万条。
func (s *Server) loadCustomersForExport(c *gin.Context) ([]storage.Customer, map[uint64]string, error) {
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
		return nil, nil, err
	}
	for i := range rows {
		s.decryptCustomer(&rows[i])
	}
	return rows, s.storeNameMap(c.Request.Context()), nil
}

// customerExcelRow 导出行（文本化便于阅读）
type customerExcelRow struct {
	ID         uint64
	PersonType string
	Name       string
	IDCardNo   string
	StaffNo    string
	Department string
	Gender     string
	BirthDate  string
	StoreID    uint64
	StoreName  string
	Status     string
	CreatedAt  string
}

func buildCustomerExcelRows(rows []storage.Customer, storeNames map[uint64]string) []customerExcelRow {
	out := make([]customerExcelRow, 0, len(rows))
	personTypeText := map[int8]string{0: "顾客", 1: "内部人员"}
	genderText := map[int8]string{0: "未知", 1: "男", 2: "女"}
	statusText := map[int8]string{0: "正常", 1: "黑名单", 2: "注销", 3: "离职"}
	for _, r := range rows {
		birth := ""
		if r.BirthDate > 0 {
			birth = time.Unix(r.BirthDate, 0).UTC().Format("2006-01-02")
		}
		out = append(out, customerExcelRow{
			ID:         r.ID,
			PersonType: personTypeText[r.PersonType],
			Name:       r.Name,
			IDCardNo:   r.IDCardNo,
			StaffNo:    derefStr(r.StaffNo),
			Department: r.Department,
			Gender:     genderText[r.Gender],
			BirthDate:  birth,
			StoreID:    r.StoreID,
			StoreName:  storeNames[r.StoreID],
			Status:     statusText[r.Status],
			CreatedAt:  time.Unix(r.CreatedAt, 0).UTC().Format("2006-01-02 15:04:05"),
		})
	}
	return out
}

// GET /export/customers.csv 人员库导出（CSV）
func (s *Server) exportCustomersCSV(c *gin.Context) {
	rows, storeNames, err := s.loadCustomersForExport(c)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

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

// GET /export/customers.xlsx 人员库导出（Excel，参数同 CSV）
func (s *Server) exportCustomersXLSX(c *gin.Context) {
	rows, storeNames, err := s.loadCustomersForExport(c)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	items := buildCustomerExcelRows(rows, storeNames)

	f := excelize.NewFile()
	sheet := "人员档案"
	if _, err := f.NewSheet(sheet); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	f.DeleteSheet("Sheet1") // 删除默认 sheet
	header := []string{"ID", "人员类型", "姓名", "身份证号", "工号", "部门", "性别", "出生日期",
		"门店ID", "门店名称", "状态", "建档时间"}
	for i, h := range header {
		cell, _ := excelize.ColumnNumberToName(i + 1)
		_ = f.SetCellValue(sheet, cell+"1", h)
	}
	sid, _ := f.NewStyle(&excelize.Style{
		Font:      &excelize.Font{Bold: true},
		Fill:      excelize.Fill{Type: "pattern", Color: []string{"D9E1F2"}, Pattern: 1},
		Alignment: &excelize.Alignment{Horizontal: "center", Vertical: "center"},
	})
	for i := range header {
		cell, _ := excelize.ColumnNumberToName(i + 1)
		_ = f.SetCellStyle(sheet, cell+"1", cell+"1", sid)
	}
	_ = f.SetRowHeight(sheet, 1, 22)
	for idx, r := range items {
		row := idx + 2
		vals := []any{r.ID, r.PersonType, r.Name, r.IDCardNo, r.StaffNo, r.Department, r.Gender,
			r.BirthDate, r.StoreID, r.StoreName, r.Status, r.CreatedAt}
		for j, v := range vals {
			cell, _ := excelize.ColumnNumberToName(j + 1)
			_ = f.SetCellValue(sheet, cell+strconv.Itoa(row), v)
		}
	}
	_ = f.SetColWidth(sheet, "A", "A", 8)
	_ = f.SetColWidth(sheet, "B", "B", 10)
	_ = f.SetColWidth(sheet, "C", "C", 14)
	_ = f.SetColWidth(sheet, "D", "D", 22)
	_ = f.SetColWidth(sheet, "E", "E", 12)
	_ = f.SetColWidth(sheet, "F", "F", 14)
	_ = f.SetColWidth(sheet, "G", "G", 8)
	_ = f.SetColWidth(sheet, "H", "H", 12)
	_ = f.SetColWidth(sheet, "J", "J", 20)
	_ = f.SetColWidth(sheet, "L", "L", 20)

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	c.Header("Content-Type", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")
	c.Header("Content-Disposition", `attachment; filename="customers.xlsx"`)
	_, _ = c.Writer.Write(buf.Bytes())
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