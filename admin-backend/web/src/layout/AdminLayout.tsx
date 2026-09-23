import { Outlet, useLocation, useNavigate } from 'react-router-dom'
import { Button, Layout, Menu, Space, Typography, theme } from 'antd'
import {
  DashboardOutlined,
  DeploymentUnitOutlined,
  TeamOutlined,
  BarChartOutlined,
  SearchOutlined,
  ShopOutlined,
  DatabaseOutlined,
} from '@ant-design/icons'
import { LogoutOutlined, FileSearchOutlined, SafetyOutlined } from '@ant-design/icons'
import { clearToken } from '../api/client'

const { Sider, Header, Content } = Layout

const MENU = [
  { key: '/', icon: <DashboardOutlined />, label: '概览' },
  { key: '/devices', icon: <DeploymentUnitOutlined />, label: '设备管理' },
  { key: '/customers', icon: <TeamOutlined />, label: '人员库管理' },
  { key: '/stats', icon: <BarChartOutlined />, label: '客流统计' },
  { key: '/staff', icon: <TeamOutlined />, label: '员工通行' },
  { key: '/history', icon: <SearchOutlined />, label: '历史来访回查' },
  { key: '/stores', icon: <ShopOutlined />, label: '门店管理', roles: ['admin', 'operator'] },
  { key: '/records', icon: <DatabaseOutlined />, label: '记录查询' },
  { key: '/tokens', icon: <SafetyOutlined />, label: '令牌管理', roles: ['admin', 'operator'] },
  { key: '/users', icon: <TeamOutlined />, label: '用户管理', roles: ['admin'] },
  { key: '/audit-logs', icon: <FileSearchOutlined />, label: '审计日志', roles: ['admin'] },
]

// 按角色过滤菜单（未标 roles 视为所有人可见）
const visibleMenu = MENU.filter((m) => !m.roles || (m.roles as string[]).includes(localStorage.getItem('role') ?? ''))

export default function AdminLayout() {
  const nav = useNavigate()
  const loc = useLocation()
  const { token } = theme.useToken()

  const selected = visibleMenu.find((m) => loc.pathname.startsWith(m.key) && m.key !== '/')
    ?.key ?? '/'

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider width={220} theme="dark">
        <div style={{ padding: 16, color: '#fff', fontWeight: 600, fontSize: 15 }}>
          🏪 FaceFlow
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[selected]}
          items={visibleMenu}
          onClick={({ key }) => nav(key)}
        />
      </Sider>
      <Layout>
        <Header style={{ background: token.colorBgContainer, paddingInline: 24, display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <Typography.Title level={4} style={{ margin: 0 }}>
            {visibleMenu.find((m) => m.key === selected)?.label ?? ''}
          </Typography.Title>
          <Space>
            <Typography.Text>{localStorage.getItem('username') ?? ''}</Typography.Text>
            <Button icon={<LogoutOutlined />} size="small" onClick={() => { clearToken(); nav('/login') }}>退出</Button>
          </Space>
        </Header>
        <Content style={{ padding: 24 }}>
          <Outlet />
        </Content>
      </Layout>
    </Layout>
  )
}