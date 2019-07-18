/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as backgroundAPI from './api/background'

// Utils
import { debounce } from '../common/debounce'
import newTab from './containers/newTab';
import NewPrivateTabPage from './containers/privateTab';

const keyName = 'new-tab-data'

const defaultState: NewTab.State = {
  showBackgroundImage: false,
  showStats: false,
  showClock: false,
  showTopSites: false,
  showSettings: false,
  topSites: [],
  ignoredTopSites: [],
  pinnedTopSites: [],
  gridSites: [],
  showEmptyPage: false,
  isIncognito: chrome.extension.inIncognitoContext,
  useAlternativePrivateSearchEngine: false,
  isTor: false,
  isQwant: false,
  bookmarks: {},
  stats: {
    adsBlockedStat: 0,
    trackersBlockedStat: 0,
    javascriptBlockedStat: 0,
    httpsUpgradesStat: 0,
    fingerprintingBlockedStat: 0
  }
}

export const getLoadTimeData = (state: NewTab.State) => {
  state = { ...state }
  state.stats = defaultState.stats
  if (chrome.extension.inIncognitoContext) {
    state.isTor = window.loadTimeData.getBoolean('isTor')
    state.isQwant = window.loadTimeData.getBoolean('isQwant')
    state.useAlternativePrivateSearchEngine =
      window.loadTimeData.getBoolean('useAlternativePrivateSearchEngine')
  }
  return state
}

const cleanData = (state: NewTab.State): NewTab.PersistantState => {
  state = { ...state }
  // Don't save items which we aren't the source
  // of data for.
  delete state.stats
  return state
}

export const load = (): NewTab.State => {
  const data: string | undefined = window.localStorage.getItem(keyName)
  let state = defaultState
  let storedState: NewTab.PersistantState
  if (data) {
    try {
      storedState = JSON.parse(data)
      // add defaults for non-peristant data
      state = {
        ...defaultState,
        ...storedState
      }
    } catch (e) {
      console.error('Could not parse local storage data: ', e)
    }
  }
  state.backgroundImage = backgroundAPI.randomBackgroundImage()
  state = getLoadTimeData(state)
  return state
}

export const debouncedSave = debounce<NewTab.State>((data: NewTab.State) => {
  if (data) {
    window.localStorage.setItem(keyName, JSON.stringify(cleanData(data)))
  }
}, 50)
