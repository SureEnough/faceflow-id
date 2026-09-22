import { useCallback, useEffect, useState } from 'react'
import {
  Alert,
  Button,
  Form,
  Input,
  Layout,
  Menu,
  Modal,
  Space,
  message,
} from 'antd'
import type { MenuProps } from 'antd'
import {
  ApiOutlined,
  CameraOutlined,
  DashboardOutlined,
  HistoryOutlined,
  SettingOutlined,
} from '@ant-design/icons'
import { clearAuthB64, getAuthB64, setAuthB64, triggerRestart as restart } from './api'
import StatusTab from './pages/StatusTab'
import ConfigTab from './pages/ConfigTab'
import CamerasTab from './pages/CamerasTab'
import SnapshotsTab from './pages/SnapshotsTab'

const { Header, Sider, Content } = Layout

type PageKey = 'dashboard' | 'cameras' | 'snapshots' | 'config'

const MENU_ITEMS: MenuProps['items'] = [
  { key: 'dashboard', icon: <DashboardOutlined />, label: '仪表盘' },
  { key: 'cameras', icon: <CameraOutlined />, label: '摄像头管理' },
  { key: 'snapshots', icon: <HistoryOutlined />, label: '最近抓拍记录' },
  { key: 'config', icon: <SettingOutlined />, label: '系统配置' },
]

const PAGE_TITLE: Record<PageKey, string> = {
  dashboard: '仪表盘',
  cameras: '摄像头管理',
  snapshots: '最近抓拍记录',
  config: '系统配置',
}

export default function App() {
  const [authed, setAuthed] = useState(() => getAuthB64().length > 0)
  const [loginOpen, setLoginOpen] = useState(!getAuthB64())
  const [loginLoading, setLoginLoading] = useState(false)
  const [restarting, setRestarting] = useState(false)
  const [refreshKey, setRefreshKey] = useState(0)
  const [page, setPage] = useState<PageKey>('dashboard')

  const refresh = useCallback(() => setRefreshKey((k) => k + 1), [])

  useEffect(() => {
    const onAuth = () => {
      clearAuthB64()
      setAuthed(false)
      setLoginOpen(true)
    }
    window.addEventListener('edgebox:auth', onAuth)
    return () => window.removeEventListener('edgebox:auth', onAuth)
  }, [])

  const onLogin = async ({ username, password }: { username: string; password: string }) => {
    setLoginLoading(true)
    try {
      // 先探测鉴权是否通过：带凭证请求 /config
      const resp = await fetch('/api/config', {
        headers: { Authorization: `Basic ${btoa(`${username}:${password}`)}` },
      })
      if (resp.status === 401) {
        message.error('账号或密码错误')
        return
      }
      setAuthB64(btoa(`${username}:${password}`))
      setAuthed(true)
      setLoginOpen(false)
      refresh()
    } finally {
      setLoginLoading(false)
    }
  }

  const onLogout = () => {
    clearAuthB64()
    setAuthed(false)
    setLoginOpen(true)
  }

  const onRestart = async () => {
    setRestarting(true)
    try {
      await restart()
      message.success('重启指令已下发，进程即将退出（由守护进程拉起）')
    } catch {
      message.warning('重启指令发送失败（连接可能已断开）')
    } finally {
      setRestarting(false)
    }
  }

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider width={220} theme="dark">
        <div style={{ padding: 16, color: '#fff', fontWeight: 600, fontSize: 15 }}>
          🏪 FaceFlow 边缘盒
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[page]}
          items={MENU_ITEMS}
          onClick={({ key }) => setPage(key as PageKey)}
        />
      </Sider>
      <Layout>
        <Header style={{ background: '#fff', paddingInline: 24, display: 'flex', alignItems: 'center', gap: 16 }}>
          <ApiOutlined style={{ color: '#1677ff' }} />
          <span style={{ fontSize: 15, fontWeight: 600 }}>{PAGE_TITLE[page]}</span>
          <span style={{ opacity: 0.6, fontSize: 13 }}>Edge-box Web Console</span>
          <div style={{ flex: 1 }} />
          {authed ? (
            <Space>
              <Button size="small" danger onClick={onRestart} loading={restarting}>
                重启进程
              </Button>
              <Button size="small" onClick={onLogout}>
                退出登录
              </Button>
            </Space>
          ) : null}
        </Header>
        <Content style={{ padding: 16 }}>
          {authed ? (
            <div key={refreshKey}>
              {page === 'dashboard' && <StatusTab />}
              {page === 'cameras' && <CamerasTab onSaved={refresh} />}
              {page === 'snapshots' && <SnapshotsTab />}
              {page === 'config' && <ConfigTab onSaved={refresh} />}
            </div>
          ) : (
            <Alert type="info" showIcon message="请登录以查看与配置边缘盒" style={{ maxWidth: 480, margin: '40px auto' }} />
          )}
        </Content>
      </Layout>
      <Modal
        title="登录边缘盒"
        open={loginOpen}
        closable={false}
        maskClosable={false}
        footer={null}
      >
        <Form onFinish={onLogin} layout="vertical">
          <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input autoFocus />
          </Form.Item>
          <Form.Item name="password" label="密码" rules={[{ required: true, message: '请输入密码' }]}>
            <Input.Password />
          </Form.Item>
          <Button type="primary" htmlType="submit" loading={loginLoading} block>
            登录
          </Button>
        </Form>
      </Modal>
    </Layout>
  )
}