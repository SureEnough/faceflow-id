import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom'
import { ConfigProvider } from 'antd'
import zhCN from 'antd/locale/zh_CN'
import AdminLayout from './layout/AdminLayout'
import Login from './pages/Login'
import Dashboard from './pages/Dashboard'
import Devices from './pages/Devices'
import Customers from './pages/Customers'
import Stats from './pages/Stats'
import HistorySearch from './pages/HistorySearch'
import Users from './pages/Users'
import Tokens from './pages/Tokens'
import AuditLogs from './pages/AuditLogs'
import Stores from './pages/Stores'
import Records from './pages/Records'
import { getToken } from './api/client'
import 'antd/dist/reset.css'

// 简单路由守卫：未登录跳转登录页
function RequireAuth({ children }: { children: JSX.Element }) {
  return getToken() ? children : <Navigate to="/login" replace />
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ConfigProvider locale={zhCN}>
      <BrowserRouter>
        <Routes>
          <Route path="/login" element={<Login />} />
          <Route element={<RequireAuth><AdminLayout /></RequireAuth>}>
            <Route path="/" element={<Dashboard />} />
            <Route path="/devices" element={<Devices />} />
            <Route path="/customers" element={<Customers />} />
            <Route path="/stats" element={<Stats />} />
            <Route path="/history" element={<HistorySearch />} />
            <Route path="/users" element={<Users />} />
            <Route path="/stores" element={<Stores />} />
            <Route path="/records" element={<Records />} />
            <Route path="/tokens" element={<Tokens />} />
            <Route path="/audit-logs" element={<AuditLogs />} />
          </Route>
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </ConfigProvider>
  </StrictMode>,
)