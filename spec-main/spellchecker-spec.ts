import { BrowserWindow } from 'electron'

import { expect } from 'chai'
import * as path from 'path'
import { closeWindow } from './window-helpers'
import { emittedOnce } from './events-helpers'
import { ifit } from './spec-helpers'

describe('spellchecker', () => {
  let w: BrowserWindow
  let hasRobotJS = false
  try {
    // We have other tests that check if native modules work, if we fail to require
    // robotjs let's skip this test to avoid false negatives
    require('robotjs')
    hasRobotJS = true
  } catch (err) { console.error(err) }

  beforeEach(async () => {
    w = new BrowserWindow({
      show: process.platform === 'win32'
    })
    await w.loadFile(path.resolve(__dirname, './fixtures/chromium/spellchecker.html'))
  })

  afterEach(async () => {
    await closeWindow(w)
  })

  function triggerContextMenu () {
    const offset = { x: 43, y: 42 }
    if (process.platform !== 'win32') {
      w.webContents.sendInputEvent({
        type: 'mouseDown',
        button: 'right',
        x: offset.x,
        y: offset.y
      })
    } else {
      require('robotjs').moveMouse(w.getContentBounds().x + offset.x, w.getContentBounds().y + offset.y)
      require('robotjs').mouseClick('right')
    }
  }

  ifit(process.platform !== 'win32' || hasRobotJS)('should detect correctly spelled words as correct', async () => {
    await w.webContents.executeJavaScript('document.body.querySelector("textarea").value = "Beautiful and lovely"')
    await w.webContents.executeJavaScript('document.body.querySelector("textarea").focus()')
    const contextMenuPromise = emittedOnce(w.webContents, 'context-menu')
    // Wait for spellchecker to load
    await new Promise(resolve => setTimeout(resolve, 1200))
    triggerContextMenu()
    const contextMenuParams: Electron.ContextMenuParams = (await contextMenuPromise)[1]
    expect(contextMenuParams.misspelledWord).to.eq('')
    expect(contextMenuParams.dictionarySuggestions).to.have.lengthOf(0)
  })

  ifit(process.platform !== 'win32' || hasRobotJS)('should detect incorrectly spelled words as incorrect', async () => {
    await w.webContents.executeJavaScript('document.body.querySelector("textarea").value = "Beautifulllll asd asd"')
    await w.webContents.executeJavaScript('document.body.querySelector("textarea").focus()')
    const contextMenuPromise = emittedOnce(w.webContents, 'context-menu')
    // Wait for spellchecker to load
    await new Promise(resolve => setTimeout(resolve, 1200))
    triggerContextMenu()
    const contextMenuParams: Electron.ContextMenuParams = (await contextMenuPromise)[1]
    expect(contextMenuParams.misspelledWord).to.eq('Beautifulllll')
    expect(contextMenuParams.dictionarySuggestions).to.have.length.of.at.least(1)
  })
})
