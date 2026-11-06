import { defineConfig } from 'vite'
import { resolve } from 'path'

// Output into ../build/temp/www; pack_www.py embeds it in the EBOOT, which extracts it to the memstick.
export default defineConfig({
  base: './',
  build: {
    outDir: resolve(__dirname, '../build/temp/www'),
    emptyOutDir: true,
    target: 'es2022',
    chunkSizeWarningLimit: 6000,
  },
  server: {
    port: 5173,
  },
})
