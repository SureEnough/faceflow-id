// ECharts 封装：手动实例化，自动 resize/dispose（避免第三方 React 包装的版本耦合）
import { useEffect, useRef } from 'react'
import * as echarts from 'echarts'
import type { EChartsOption } from 'echarts'

interface Props {
  option: EChartsOption
  height?: number
}

export default function EChart({ option, height = 320 }: Props) {
  const ref = useRef<HTMLDivElement>(null)
  const chartRef = useRef<echarts.ECharts>()

  useEffect(() => {
    if (!ref.current) return
    chartRef.current = echarts.init(ref.current)
    const onResize = () => chartRef.current?.resize()
    window.addEventListener('resize', onResize)
    return () => {
      window.removeEventListener('resize', onResize)
      chartRef.current?.dispose()
    }
  }, [])

  useEffect(() => {
    if (chartRef.current) chartRef.current.setOption(option, true)
  }, [option])

  return <div ref={ref} style={{ width: '100%', height }} />
}