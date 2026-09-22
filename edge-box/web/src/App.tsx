import { useCallback, useEffect, useState } from 'react'
import {
  Alert,
  Button,
  Form,
  Input,
  Layout,
  Modal,
  Space,
  Tabs,
  message,
} from 'antd'
import {
  ApiOutlined,
  CameraOutlined,
  DashboardOutlined,
  SettingOutlined,
} from '@ant-design/icons'
import { clearAuthB64, getAuthB64, setAuthB64, triggerRestart as restart } from './api'
import StatusTab from './pages/StatusTab'
import ConfigTab from './pages/ConfigTab'
import CamerasTab from './pages/CamerasTab'

const { Header, Content } = Layout

export default function App() {
  const [authed, setAuthed] = useState(() => getAuthB64().length > 0)
  const [loginOpen, setLoginOpen] = useState(!getAuthB64())
  const [loginLoading, setLoginLoading] = useState(false)
  const [restarting, setRestarting] = useState(false)
  const [refreshKey, setRefreshKey] = useState(0)

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
      <Header style={{ color: '#fff', display: 'flex', alignItems: 'center', gap: 16 }}>
        <ApiOutlined />
        <span style={{ fontSize: 16, fontWeight: 600 }}>FaceFlow 边缘盒配置</span>
        <span style={{ opacity: 0.7, fontSize: 13 }}>Edge-box Web Console</span>
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
      <Content style={{ padding: 16, maxWidth: 1200, width: '100%', margin: '0 auto' }}>
        {authed ? (
          <Tabs
            key={refreshKey}
            defaultActiveKey="status"
            items={[
              { key: 'status', label: <span><DashboardOutlined /> 运行状态</span>, children: <StatusTab /> },
              { key: 'config', label: <span><SettingOutlined /> 全局参数</span>, children: <ConfigTab onSaved={refresh} /> },
              { key: 'cameras', label: <span><CameraOutlined /> 摄像头</span>, children: <CamerasTab onSaved={refresh} /> },
            ]}
          />
        ) : (
          <Alert type="info" showIcon message="请登录以查看与配置边缘盒" style={{ maxWidth: 480, margin: '40px auto' }} />
        )}
      </Content>
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