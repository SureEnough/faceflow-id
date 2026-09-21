package api

import (
	"fmt"
	"net/http"
	"time"

	"admin-backend/internal/storage"

	"github.com/gin-gonic/gin"
)

// GET /stats/flow 顾客客流统计（默认仅顾客 person_type=0）
func (s *Server) statsFlow(c *gin.Context) {
	gran := c.DefaultQuery("granularity", "day")
	start, end, err := parseRange(c)
	if err != nil {
		Fail(c, http.StatusBadRequest, CodeParam, err.Error())
		return
	}
	personType := storage.PersonTypeCustomer
	if c.Query("person_type") == "1" {
		personType = storage.PersonTypeStaff
	}

	dateExpr := map[string]string{
		"hour":  "%Y-%m-%d %H:00",
		"day":   "%Y-%m-%d",
		"week":  "%Y-%W",
		"month": "%Y-%m",
	}[gran]
	if dateExpr == "" {
		Fail(c, http.StatusBadRequest, CodeParam, "granularity must be hour/day/week/month")
		return
	}

	// 兼容 SQLite(strftime) 与 MySQL(DATE_FORMAT)。
	// 注意：格式串必须内联为字面量（参数化格式串在 GROUP BY 聚合场景下有兼容性问题）
	dateFn := fmt.Sprintf("FROM_UNIXTIME(created_at, '%s')", dateExpr)
	if s.db.Dialector.Name() == "sqlite" {
		dateFn = fmt.Sprintf("strftime('%s', created_at, 'unixepoch')", dateExpr)
	}
	q := `SELECT ` + dateFn + ` AS bucket,
	         SUM(CASE WHEN direction = 0 THEN 1 ELSE 0 END) AS flow_in,
	         SUM(CASE WHEN direction = 1 THEN 1 ELSE 0 END) AS flow_out
	      FROM recognition_logs
	      WHERE person_type = ? AND created_at BETWEEN ? AND ?`
	args := []any{personType, start.Unix(), end.Unix()}
	if ids := c.QueryArray("device_ids"); len(ids) > 0 {
		q += " AND device_id IN (?)"
		args = append(args, ids)
	}
	q += " GROUP BY bucket ORDER BY bucket"

	var rows []storage.VisitStatsRow
	if err := s.db.Raw(q, args...).Scan(&rows).Error; err != nil {
		Fail(c, http.StatusInternalServerError, CodeServer, err.Error())
		return
	}

	var totalIn, totalOut int64
	items := make([]gin.H, 0, len(rows))
	for _, r := range rows {
		totalIn += r.In
		totalOut += r.Out
		items = append(items, gin.H{"bucket": r.Bucket, "in": r.In, "out": r.Out})
	}
	OK(c, gin.H{"items": items, "total": gin.H{"in": totalIn, "out": totalOut}})
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