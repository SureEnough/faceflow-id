package api

import (
	"fmt"
	"net/http"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// flowParams 客流统计查询参数
type flowParams struct {
	gran          string // hour/day/week/month
	personType    int8
	start, end    time.Time
	deviceIDs     []string
	storeIDs      []string
	groupByCamera bool
	unique        bool // 统计唯一识别人数（跨摄像头/跨记录去重）
}

// flowRow 客流统计一行（可选按摄像头拆分）
type flowRow struct {
	Bucket   string `json:"bucket"`
	CameraID string `json:"camera_id,omitempty"`
	In       int64  `json:"in" gorm:"column:flow_in"`
	Out      int64  `json:"out" gorm:"column:flow_out"`
}

func parseFlowParams(c *gin.Context) (*flowParams, error) {
	gran := c.DefaultQuery("granularity", "day")
	switch gran {
	case "hour", "day", "week", "month":
	default:
		return nil, fmt.Errorf("granularity must be hour/day/week/month")
	}
	start, end, err := parseRange(c)
	if err != nil {
		return nil, err
	}
	personType := int8(storage.PersonTypeCustomer)
	if c.Query("person_type") == "1" {
		personType = storage.PersonTypeStaff
	}
	return &flowParams{
		gran:          gran,
		personType:    personType,
		start:         start,
		end:           end,
		deviceIDs:     c.QueryArray("device_ids"),
		storeIDs:      c.QueryArray("store_ids"),
		groupByCamera: c.Query("group_by") == "camera",
		unique:        c.Query("unique") == "1",
	}, nil
}

// dateExprSQL 返回数据库方言的时间分桶表达式（SQLite / MySQL 均可）
func dateExprSQL(dbName, gran string) string {
	expr := map[string]string{
		"hour":  "%Y-%m-%d %H:00",
		"day":   "%Y-%m-%d",
		"week":  "%Y-%W",
		"month": "%Y-%m",
	}[gran]
	if dbName == "sqlite" {
		return fmt.Sprintf("strftime('%s', created_at, 'unixepoch')", expr)
	}
	// 格式串内联为字面量（参数化格式串在 GROUP BY 聚合场景下有兼容性问题）
	return fmt.Sprintf("FROM_UNIXTIME(created_at, '%s')", expr)
}

// flowData 执行客流统计查询（stats/flow 与导出 CSV 复用）
func (s *Server) flowData(c *gin.Context, p *flowParams) ([]flowRow, int64, int64, int64, error) {
	dateFn := dateExprSQL(s.db.Dialector.Name(), p.gran)
	groupCols, orderCols := "bucket", "bucket"
	selectCols := dateFn + ` AS bucket`
	if p.groupByCamera {
		selectCols += `, COALESCE(camera_id,'') AS camera_id`
		groupCols, orderCols = "bucket, camera_id", "camera_id, bucket"
	}
	q := `SELECT ` + selectCols + `,
	         SUM(CASE WHEN direction = 0 THEN 1 ELSE 0 END) AS flow_in,
	         SUM(CASE WHEN direction = 1 THEN 1 ELSE 0 END) AS flow_out
	      FROM recognition_logs
	      WHERE person_type = ? AND created_at BETWEEN ? AND ?`
	args := []any{p.personType, p.start.Unix(), p.end.Unix()}
	q, args = appendFlowFilters(q, args, p)
	q += " GROUP BY " + groupCols + " ORDER BY " + orderCols

	var rows []flowRow
	if err := s.db.Raw(q, args...).Scan(&rows).Error; err != nil {
		return nil, 0, 0, 0, err
	}
	var totalIn, totalOut int64
	for _, r := range rows {
		totalIn += r.In
		totalOut += r.Out
	}

	// 唯一识别人数：时间段内命中人员档案（customer_id 非空）的去重计数，跨摄像头/记录去重
	var uniquePersons int64
	if p.unique {
		uq := `SELECT COUNT(DISTINCT customer_id) FROM recognition_logs
		       WHERE person_type = ? AND customer_id IS NOT NULL AND created_at BETWEEN ? AND ?`
		uargs := []any{p.personType, p.start.Unix(), p.end.Unix()}
		uq, uargs = appendFlowFilters(uq, uargs, p)
		if err := s.db.Raw(uq, uargs...).Scan(&uniquePersons).Error; err != nil {
			return nil, 0, 0, 0, err
		}
	}
	return rows, totalIn, totalOut, uniquePersons, nil
}

// appendFlowFilters 追加设备/门店过滤条件
func appendFlowFilters(q string, args []any, p *flowParams) (string, []any) {
	if len(p.deviceIDs) > 0 {
		q += " AND device_id IN (?)"
		args = append(args, p.deviceIDs)
	}
	if len(p.storeIDs) > 0 {
		q += " AND device_id IN (SELECT id FROM devices WHERE store_id IN (?))"
		args = append(args, p.storeIDs)
	}
	return q, args
}

// GET /stats/flow 顾客客流统计（默认仅顾客 person_type=0）
// 可选参数：granularity / start_at / end_at / person_type / device_ids / store_ids / group_by=camera / unique=1
func (s *Server) statsFlow(c *gin.Context) {
	p, err := parseFlowParams(c)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	rows, totalIn, totalOut, uniquePersons, err := s.flowData(c, p)
	if err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		item := gin.H{"bucket": r.Bucket, "in": r.In, "out": r.Out}
		if p.groupByCamera {
			item["camera_id"] = r.CameraID
		}
		items = append(items, item)
	}
	total := gin.H{"in": totalIn, "out": totalOut}
	if p.unique {
		total["unique_persons"] = uniquePersons
	}
	OK(c, gin.H{"items": items, "total": total})
}

// GET /stats/staff 内部人员通行记录/统计
func (s *Server) statsStaff(c *gin.Context) {
	start, end, err := parseRange(c)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	q := `SELECT c.id AS customer_id, COALESCE(c.staff_no,'') AS staff_no, COALESCE(c.department,'') AS department,
	         SUM(CASE WHEN l.direction = 0 THEN 1 ELSE 0 END) AS flow_in,
	         SUM(CASE WHEN l.direction = 1 THEN 1 ELSE 0 END) AS flow_out,
	         MAX(l.created_at) AS last_in
	      FROM recognition_logs l JOIN customers c ON c.id = l.customer_id
	      WHERE l.person_type = ? AND l.created_at BETWEEN ? AND ?`
	args := []any{storage.PersonTypeStaff, start.Unix(), end.Unix()}
	if v := c.Query("staff_no"); v != "" {
		q += " AND c.staff_no = ?"
		args = append(args, v)
	}
	if v := c.Query("department"); v != "" {
		q += " AND c.department = ?"
		args = append(args, v)
	}
	q += " GROUP BY c.id, c.staff_no, c.department"

	type staffRow struct {
		CustomerID uint64 `json:"customer_id"`
		StaffNo    string `json:"staff_no"`
		Department string `json:"department"`
		In         int64  `json:"in" gorm:"column:flow_in"`
		Out        int64  `json:"out" gorm:"column:flow_out"`
		LastIn     int64  `json:"last_in"` // Unix 秒
	}
	var rows []staffRow
	if err := s.db.Raw(q, args...).Scan(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}
	// 时间戳转 ISO8601 输出
	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		items = append(items, gin.H{
			"customer_id": r.CustomerID,
			"staff_no":    r.StaffNo,
			"department":  r.Department,
			"in":          r.In,
			"out":         r.Out,
			"last_in":     formatTime(r.LastIn),
		})
	}
	OK(c, gin.H{"items": items})
}

// GET /stats/visits/:customer_id 单顾客来访统计
func (s *Server) statsVisits(c *gin.Context) {
	id := parseUintOrZero(c.Param("customer_id"))
	var v storage.Visits
	if err := s.db.First(&v, id).Error; err != nil {
		// 未物化时实时计算
		v, err = s.realtimeVisits(id)
		if err != nil {
			Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
			return
		}
	}
	OK(c, gin.H{
		"customer_id":    v.CustomerID,
		"total_visits":   v.TotalVisits,
		"visit_days":     v.VisitDays,
		"first_visit_at": formatTime(v.FirstVisitAt),
		"last_visit_at":  formatTime(v.LastVisitAt),
	})
}

func (s *Server) realtimeVisits(id uint64) (storage.Visits, error) {
	var v storage.Visits
	dayExpr := "FROM_UNIXTIME(created_at, '%Y-%m-%d')"
	if s.db.Dialector.Name() == "sqlite" {
		dayExpr = "DATE(created_at, 'unixepoch')"
	}
	q := `SELECT :id AS customer_id,
	         COUNT(*) AS total_visits,
	         COUNT(DISTINCT ` + dayExpr + `) AS visit_days,
	         MIN(created_at) AS first_visit_at,
	         MAX(created_at) AS last_visit_at
	      FROM recognition_logs
	      WHERE customer_id = ? AND person_type = 0`
	err := s.db.Raw(q, id, id).Scan(&v).Error
	return v, err
}

func parseRange(c *gin.Context) (time.Time, time.Time, error) {
	start, err := time.Parse(time.RFC3339, c.Query("start_at"))
	if err != nil {
		return time.Time{}, time.Time{}, err
	}
	end, err := time.Parse(time.RFC3339, c.DefaultQuery("end_at", time.Now().UTC().Format(time.RFC3339)))
	return start, end, err
}