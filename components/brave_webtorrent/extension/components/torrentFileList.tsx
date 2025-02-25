/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as React from 'react'
import { Table } from 'brave-ui/components'
import { Cell, Row } from 'brave-ui/components/dataTables/table/index'
import { LoaderIcon } from 'brave-ui/components/icons'
import * as prettierBytes from 'prettier-bytes'

// Constants
import { File, TorrentObj } from '../constants/webtorrentState'

interface Props {
  torrentId: string
  torrent?: TorrentObj
}

export default class TorrentFileList extends React.PureComponent<Props, {}> {
  render () {
    const { torrent } = this.props
    if (!torrent || !torrent.files) {
      return (
        <div className='torrentSubhead'>
          <p>
            Click "Start Torrent" to begin your download. WebTorrent can be
            disabled from the extensions panel in Settings.
          </p>
        </div>
      )
    }

    const header: Cell[] = [
      {
        content: '#'
      },
      {
        content: 'Name'
      },
      {
        content: 'Save File'
      },
      {
        content: 'Size'
      }
    ]

    const renderFileLink = (file: File, ix: number, isDownload: boolean) => {
      const { torrentId, torrent } = this.props
      if (!torrent) return null
      if (isDownload) {
        if (torrent.serverURL) {
          const url = torrent.serverURL + '/' + ix
          return (
            <a href={url} download={file.name}>
              ⇩
            </a>
          )
        } else {
          return <div /> // No download links until the server is ready
        }
      } else {
        // use # for .torrent links, since query params might cause the remote
        // server to return 404
        const suffix = /^https?:/.test(torrentId) ? '#ix=' + ix : '&ix=' + ix
        const href = torrentId + suffix
        return (
          <a href={href} target='_blank'>
            {' '}
            {file.name}{' '}
          </a>
        )
      }
    }

    const rows: Row[] = torrent.files.map((file: File, index: number) => {
      return {
        content: [
          {
            content: index + 1
          },
          {
            content: renderFileLink(file, index, false)
          },
          {
            content: renderFileLink(file, index, true)
          },
          {
            content: prettierBytes(file.length)
          }
        ]
      }
    })

    return (
      <div>
        <Table header={header} rows={rows}>
          <div className='loadingContainer'>
            <div className='__icon'>
              <LoaderIcon />
            </div>
            Loading the torrent file list
          </div>
        </Table>
      </div>
    )
  }
}
