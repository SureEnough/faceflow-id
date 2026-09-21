package api

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
)

// 统一响应体（与架构文档 13.0 一致）
type Resp struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    any    `json:"data,omitempty"`
}

func OK(c *gin.Context, data any) {
	c.JSON(http.StatusOK, Resp{Code: 0, Message: "ok", Data: data})
}

func Fail(c *gin.Context, httpStatus, code int, msg string) {
	c.JSON(httpStatus, Resp{Code: code, Message: msg})
}

// 错误码（与文档 13.0 一致）
const (
	CodeParam   = 40001
	CodeUnauth  = 40100
	CodeForbid  = 40300
	CodeNotFound = 40400
	CodeRate    = 42900
	CodeServer  = 50000
)
// itoa 整数转字符串（避免多次写 strconv）
func itoa(v int) string { return strconv.Itoa(v) }
