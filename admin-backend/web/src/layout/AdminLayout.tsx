import { useEffect, useMemo, useState } from 'react'
import { Outlet, useLocation, useNavigate } from 'react-router-dom'
import { Button, Layout, Menu, Space, Typography, theme } from 'antd'
import type { MenuProps } from 'antd'
import {
  DashboardOutlined,
  DeploymentUnitOutlined,
  TeamOutlined,
  BarChartOutlined,
  SearchOutlined,
  ShopOutlined,
  DatabaseOutlined,
  SettingOutlined,
} from '@ant-design/icons'
import { LogoutOutlined, FileSearchOutlined, SafetyOutlined } from '@ant-design/icons'
import { clearToken } from '../api/client'

const { Sider, Header, Content } = Layout

interface MenuItem {
  key: string
  icon?: React.ReactNode
  label: string
  roles?: string[]
  children?: MenuItem[]
}

const MENU: MenuItem[] = [
  { key: '/', icon: <DashboardOutlined />, label: '概览' },
  { key: '/devices', icon: <DeploymentUnitOutlined />, label: '设备管理' },
  { key: '/customers', icon: <TeamOutlined />, label: '人员管理' },
  { key: '/stats', icon: <BarChartOutlined />, label: '客流统计' },
  { key: '/history', icon: <SearchOutlined />, label: '历史来访回查' },
  { key: '/stores', icon: <ShopOutlined />, label: '门店管理', roles: ['admin', 'operator'] },
  { key: '/records', icon: <DatabaseOutlined />, label: '识别记录' },
  {
    key: '/system',
    icon: <SettingOutlined />,
    label: '系统管理',
    children: [
      { key: '/tokens', icon: <SafetyOutlined />, label: '令牌管理', roles: ['admin', 'operator'] },
      { key: '/users', icon: <TeamOutlined />, label: '账号管理', roles: ['admin'] },
      { key: '/audit-logs', icon: <FileSearchOutlined />, label: '审计日志', roles: ['admin'] },
    ],
  },
]

// 按角色过滤菜单（递归；未标 roles 视为所有人可见；子项全不可见则父级隐藏）
function filterMenu(items: MenuItem[], role: string): MenuItem[] {
  const out: MenuItem[] = []
  for (const m of items) {
    if (m.children) {
      const ch = filterMenu(m.children, role)
      if (ch.length) out.push({ ...m, children: ch })
    } else if (!m.roles || m.roles.includes(role)) {
      out.push(m)
    }
  }
  return out
}

// 在菜单（含子级）中按路径找命中项，返回选中 key
function findSelected(items: MenuItem[], pathname: string): string | undefined {
  for (const m of items) {
    if (m.key !== '/' && pathname.startsWith(m.key)) return m.key
    if (m.children) {
      const hit = m.children.find((c) => pathname.startsWith(c.key))
      if (hit) return hit.key
    }
  }
  return undefined
}

// 转成 antd Menu items（剔除 roles 等内部字段）
function toAntdItems(items: MenuItem[]): MenuProps['items'] {
  return items.map((m) => (m.children
    ? { key: m.key, icon: m.icon, label: m.label, children: toAntdItems(m.children) }
    : { key: m.key, icon: m.icon, label: m.label }))
}

// 取当前标题（顶层或子级）
function findLabel(items: MenuItem[], key: string): string {
  for (const m of items) {
    if (m.key === key) return m.label
    if (m.children) {
      const hit = m.children.find((c) => c.key === key)
      if (hit) return hit.label
    }
  }
  return ''
}

export default function AdminLayout() {
  const nav = useNavigate()
  const loc = useLocation()
  const { token } = theme.useToken()

  const [role] = useState(() => localStorage.getItem('role') ?? '')
  const visibleMenu = useMemo(() => filterMenu(MENU, role), [role])
  const selected = findSelected(visibleMenu, loc.pathname) ?? '/'

  // 选中子级时自动展开所属父级
  const [openKeys, setOpenKeys] = useState<string[]>([])
  useEffect(() => {
    const parent = visibleMenu.find((m) => m.children?.some((c) => c.key === selected))
    if (parent) setOpenKeys((keys) => (keys.includes(parent.key) ? keys : [...keys, parent.key]))
  }, [selected, visibleMenu])

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
          openKeys={openKeys}
          onOpenChange={setOpenKeys}
          items={toAntdItems(visibleMenu)}
          onClick={({ key }) => nav(key)}
        />
      </Sider>
      <Layout>
        <Header style={{ background: token.colorBgContainer, paddingInline: 24, display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <Typography.Title level={4} style={{ margin: 0 }}>
            {findLabel(visibleMenu, selected)}
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