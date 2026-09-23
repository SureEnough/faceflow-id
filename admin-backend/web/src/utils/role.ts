// 前端角色工具（登录时已写入 localStorage role）
export function getRole(): string { return localStorage.getItem('role') ?? '' }

export function isAdmin(): boolean { return getRole() === 'admin' }
export function canWrite(): boolean { return ['admin', 'operator'].includes(getRole()) }
export function isViewer(): boolean { return getRole() === 'viewer' }
