import { useState } from 'react'
import { Button, Card, Form, Input, Typography, message } from 'antd'
import { LockOutlined, UserOutlined } from '@ant-design/icons'
import { useNavigate } from 'react-router-dom'
import { client, errMsg } from '../api/client'
import type { ApiResp } from '../api/client'

interface LoginResp {
  token: string
  token_type: string
  expires_in: number
  user: { id: number; username: string; role: string }
}

export default function Login() {
  const [loading, setLoading] = useState(false)
  const nav = useNavigate()

  const submit = async (values: { username: string; password: string }) => {
    setLoading(true)
    try {
      const { data } = await client.post<ApiResp<LoginResp>>('/auth/login', values)
      if (data.code !== 0) {
        message.error(data.message)
        return
      }
      localStorage.setItem('token', data.data.token)
      localStorage.setItem('username', data.data.user.username)
      localStorage.setItem('role', data.data.user.role)
      message.success(`欢迎，${data.data.user.username}（${data.data.user.role}）`)
      nav('/')
    } catch (e) {
      message.error(errMsg(e))
    } finally {
      setLoading(false)
    }
  }

  return (
    <div style={{ minHeight: '100vh', display: 'flex', alignItems: 'center', justifyContent: 'center', background: '#f0f2f5' }}>
      <Card style={{ width: 380 }} title={<Typography.Title level={4} style={{ margin: 0 }}>🏪 FaceFlow</Typography.Title>}>
        <Form onFinish={submit} layout="vertical" initialValues={{ username: 'admin', password: '' }}>
          <Form.Item name="username" label="用户名" rules={[{ required: true, message: '请输入用户名' }]}>
            <Input prefix={<UserOutlined />} placeholder="admin" autoComplete="username" />
          </Form.Item>
          <Form.Item name="password" label="密码" rules={[{ required: true, message: '请输入密码' }]}>
            <Input.Password prefix={<LockOutlined />} placeholder="admin123" autoComplete="current-password" />
          </Form.Item>
          <Button type="primary" htmlType="submit" block loading={loading}>登 录</Button>
          <Typography.Paragraph type="secondary" style={{ marginTop: 12, marginBottom: 0, fontSize: 12 }}>
            默认账号：admin / admin123（环境变量可覆盖）
          </Typography.Paragraph>
        </Form>
      </Card>
    </div>
  )
}