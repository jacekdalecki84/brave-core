import { withBackgrounds } from '@storybook/addon-backgrounds'
import { configure, addDecorator, addParameters } from '@storybook/react'
import { withOptions } from '@storybook/addon-options'
import { initLocale } from 'brave-ui/helpers'
import locale from './locale'
import { withThemesProvider } from 'storybook-addon-styled-component-theme'
import DarkTheme from 'brave-ui/theme/brave-dark'
import DefaultTheme from 'brave-ui/theme/brave-default'

// Fonts
import '../ui/webui/resources/fonts/muli.css'
import '../ui/webui/resources/fonts/poppins.css'

const themes = [DefaultTheme, DarkTheme]
addDecorator(withThemesProvider(themes))

addParameters({
  backgrounds: [
    { name: 'Neutral300', value: '#DEE2E6', default: true },
    { name: 'Grey700', value: '#5E6175' },
    { name: 'White', value: '#FFF' },
    { name: 'Grey900', value: '#1E2029' }
  ]
})

addParameters({
  options: {
    name: 'Brave Browser UI',
    url: 'https://github.com/brave/brave-core',
    panelPosition: 'right'
  }
})

function loadStories() {
  initLocale(locale)
  const req = require.context('../components/', true, /\/stories\/.*\.tsx$/)
  req.keys().forEach(filename => req(filename))
}

configure(loadStories, module)
