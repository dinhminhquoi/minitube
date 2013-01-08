#include "playlistmodel.h"
#include "videomimedata.h"
#include "videosource.h"
#include "ytsearch.h"
#include "video.h"
#include "searchparams.h"

static const int maxItems = 10;
static const QString recentKeywordsKey = "recentKeywords";
static const QString recentChannelsKey = "recentChannels";

PlaylistModel::PlaylistModel(QWidget *parent) : QAbstractListModel(parent) {
    videoSource = 0;
    searching = false;
    canSearchMore = true;
    m_activeVideo = 0;
    m_activeRow = -1;
    skip = 1;
    max = 0;

    hoveredRow = -1;
    authorHovered = false;
    authorPressed = false;
}

int PlaylistModel::rowCount(const QModelIndex &/*parent*/) const {
    int count = videos.size();
    
    // add the message item
    if (videos.isEmpty() || !searching)
        count++;
    
    return count;
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
    
    int row = index.row();
    
    if (row == videos.size()) {
        
        QPalette palette;
        QFont boldFont;
        boldFont.setBold(true);
        
        switch (role) {
        case ItemTypeRole:
            return ItemTypeShowMore;
        case Qt::DisplayRole:
        case Qt::StatusTipRole:
            if (!errorMessage.isEmpty()) return errorMessage;
            if (searching) return tr("Searching...");
            if (canSearchMore) return tr("Show %1 More").arg(maxItems);
            if (videos.isEmpty()) return tr("No videos");
            else return tr("No more videos");
        case Qt::TextAlignmentRole:
            return QVariant(int(Qt::AlignHCenter | Qt::AlignVCenter));
        case Qt::ForegroundRole:
            if (!errorMessage.isEmpty())
                return palette.color(QPalette::ToolTipText);
            else
                return palette.color(QPalette::Dark);
        case Qt::BackgroundColorRole:
            if (!errorMessage.isEmpty())
                return palette.color(QPalette::ToolTipBase);
            else
                return QVariant();
        case Qt::FontRole:
            return boldFont;
        default:
            return QVariant();
        }
        
    } else if (row < 0 || row >= videos.size())
        return QVariant();
    
    Video *video = videos.at(row);
    
    switch (role) {
    case ItemTypeRole:
        return ItemTypeVideo;
    case VideoRole:
        return QVariant::fromValue(QPointer<Video>(video));
    case ActiveTrackRole:
        return video == m_activeVideo;
    case Qt::DisplayRole:
        return video->title();
    case HoveredItemRole:
        return hoveredRow == index.row();
    case AuthorHoveredRole:
        return authorHovered;
    case AuthorPressedRole:
        return authorPressed;
    }
    
    return QVariant();
}

void PlaylistModel::setActiveRow( int row) {
    if ( rowExists( row ) ) {
        
        m_activeRow = row;
        m_activeVideo = videoAt(row);
        
        int oldactiverow = m_activeRow;
        
        if ( rowExists( oldactiverow ) )
            emit dataChanged( createIndex( oldactiverow, 0 ), createIndex( oldactiverow, columnCount() - 1 ) );
        
        emit dataChanged( createIndex( m_activeRow, 0 ), createIndex( m_activeRow, columnCount() - 1 ) );
        emit activeRowChanged(row);
        
    } else {
        m_activeRow = -1;
        m_activeVideo = 0;
    }

}

int PlaylistModel::nextRow() const {
    int nextRow = m_activeRow + 1;
    if (rowExists(nextRow))
        return nextRow;
    return -1;
}

int PlaylistModel::previousRow() const {
    int prevRow = m_activeRow - 1;
    if (rowExists(prevRow))
        return prevRow;
    return -1;
}

Video* PlaylistModel::videoAt( int row ) const {
    if ( rowExists( row ) )
        return videos.at( row );
    return 0;
}

Video* PlaylistModel::activeVideo() const {
    return m_activeVideo;
}

void PlaylistModel::setVideoSource(VideoSource *videoSource) {
    while (!videos.isEmpty())
        delete videos.takeFirst();

    m_activeVideo = 0;
    m_activeRow = -1;
    skip = 1;
    reset();

    this->videoSource = videoSource;
    connect(videoSource, SIGNAL(gotVideo(Video*)),
            SLOT(addVideo(Video*)), Qt::UniqueConnection);
    connect(videoSource, SIGNAL(finished(int)),
            SLOT(searchFinished(int)), Qt::UniqueConnection);
    connect(videoSource, SIGNAL(error(QString)),
            SLOT(searchError(QString)), Qt::UniqueConnection);

    searchMore();
}

void PlaylistModel::searchMore(int max) {
    if (searching) return;
    searching = true;
    this->max = max;
    errorMessage.clear();
    videoSource->loadVideos(max, skip);
    skip += max;
}

void PlaylistModel::searchMore() {
    searchMore(maxItems);
}

void PlaylistModel::searchNeeded() {
    int remainingRows = videos.size() - m_activeRow;
    int rowsNeeded = maxItems - remainingRows;
    if (rowsNeeded > 0)
        searchMore(rowsNeeded);
}

void PlaylistModel::abortSearch() {
    while (!videos.isEmpty())
        delete videos.takeFirst();
    reset();
    videoSource->abort();
    searching = false;
}

void PlaylistModel::searchFinished(int total) {
    searching = false;
    canSearchMore = total >= max;

    // update the message item
    emit dataChanged( createIndex( maxItems, 0 ), createIndex( maxItems, columnCount() - 1 ) );

    if (!videoSource->getSuggestions().isEmpty())
        emit haveSuggestions(videoSource->getSuggestions());
}

void PlaylistModel::searchError(QString message) {
    errorMessage = message;
    // update the message item
    emit dataChanged( createIndex( maxItems, 0 ), createIndex( maxItems, columnCount() - 1 ) );
}

void PlaylistModel::addVideo(Video* video) {
    
    connect(video, SIGNAL(gotThumbnail()), SLOT(updateThumbnail()), Qt::UniqueConnection);
    video->loadThumbnail();

    beginInsertRows(QModelIndex(), videos.size(), videos.size());
    videos << video;
    endInsertRows();
    
    // first result!
    if (videos.size() == 1) {

        // manualplay
        QSettings settings;
        if (!settings.value("manualplay", false).toBool())
            setActiveRow(0);

        if (videoSource->metaObject()->className() == QLatin1String("YTSearch")) {

            static const int maxRecentElements = 10;

            YTSearch *search = dynamic_cast<YTSearch *>(videoSource);
            SearchParams *searchParams = search->getSearchParams();

            // save keyword
            QString query = searchParams->keywords();
            if (!query.isEmpty() && !searchParams->isTransient()) {
                if (query.startsWith("http://")) {
                    // Save the video title
                    query += "|" + videos.first()->title();
                }
                QStringList keywords = settings.value(recentKeywordsKey).toStringList();
                keywords.removeAll(query);
                keywords.prepend(query);
                while (keywords.size() > maxRecentElements)
                    keywords.removeLast();
                settings.setValue(recentKeywordsKey, keywords);
            }

            // save channel
            QString channel = searchParams->author();
            if (!channel.isEmpty() && !searchParams->isTransient()) {
                if (!video->authorUri().isEmpty())
                    channel = video->authorUri() + "|" + video->author();
                QStringList channels = settings.value(recentChannelsKey).toStringList();
                channels.removeAll(channel);
                channels.prepend(channel);
                while (channels.size() > maxRecentElements)
                    channels.removeLast();
                settings.setValue(recentChannelsKey, channels);
            }
        }

    }

}

void PlaylistModel::updateThumbnail() {

    Video *video = static_cast<Video *>(sender());
    if (!video) {
        qDebug() << "Cannot get sender";
        return;
    }

    int row = rowForVideo(video);
    emit dataChanged( createIndex( row, 0 ), createIndex( row, columnCount() - 1 ) );

}

// --- item removal

/**
  * This function does not free memory
  */
bool PlaylistModel::removeRows(int position, int rows, const QModelIndex & /*parent*/) {
    beginRemoveRows(QModelIndex(), position, position+rows-1);
    for (int row = 0; row < rows; ++row) {
        videos.removeAt(position);
    }
    endRemoveRows();
    return true;
}

void PlaylistModel::removeIndexes(QModelIndexList &indexes) {
    QList<Video*> originalList(videos);
    QList<Video*> delitems;
    foreach (QModelIndex index, indexes) {
        if (index.row() >= originalList.size()) continue;
        Video* video = originalList.at(index.row());
        int idx = videos.indexOf(video);
        if (idx != -1) {
            beginRemoveRows(QModelIndex(), idx, idx);
            delitems.append(video);
            videos.removeAll(video);
            endRemoveRows();
        }
    }

    qDeleteAll(delitems);

}

// --- Sturm und drang ---



Qt::DropActions PlaylistModel::supportedDropActions() const {
    return Qt::MoveAction;
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex &index) const {
    if (index.isValid())
        if (index.row() == videos.size()) {
            // don't drag the "show 10 more" item
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        } else return (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    return Qt::ItemIsDropEnabled;
}

QStringList PlaylistModel::mimeTypes() const {
    QStringList types;
    types << "application/x-minitube-video";
    return types;
}

QMimeData* PlaylistModel::mimeData( const QModelIndexList &indexes ) const {
    VideoMimeData* mime = new VideoMimeData();

    foreach( const QModelIndex &it, indexes ) {
        int row = it.row();
        if (row >= 0 && row < videos.size())
            mime->addVideo( videos.at( it.row() ) );
    }

    return mime;
}

bool PlaylistModel::dropMimeData(const QMimeData *data,
                                 Qt::DropAction action, int row, int column,
                                 const QModelIndex &parent) {
    if (action == Qt::IgnoreAction)
        return true;

    if (!data->hasFormat("application/x-minitube-video"))
        return false;

    if (column > 0)
        return false;

    int beginRow;
    if (row != -1)
        beginRow = row;
    else if (parent.isValid())
        beginRow = parent.row();
    else
        beginRow = rowCount(QModelIndex());

    const VideoMimeData* videoMimeData = dynamic_cast<const VideoMimeData*>( data );
    if(!videoMimeData ) return false;

    QList<Video*> droppedVideos = videoMimeData->videos();
    foreach( Video *video, droppedVideos) {
        
        // remove videos
        int videoRow = videos.indexOf(video);
        removeRows(videoRow, 1, QModelIndex());
        
        // and then add them again at the new position
        beginInsertRows(QModelIndex(), beginRow, beginRow);
        videos.insert(beginRow, video);
        endInsertRows();

    }

    // fix m_activeRow after all this
    m_activeRow = videos.indexOf(m_activeVideo);

    // let the MediaView restore the selection
    emit needSelectionFor(droppedVideos);

    return true;

}

int PlaylistModel::rowForVideo(Video* video) {
    return videos.indexOf(video);
}

QModelIndex PlaylistModel::indexForVideo(Video* video) {
    return createIndex(videos.indexOf(video), 0);
}

void PlaylistModel::move(QModelIndexList &indexes, bool up) {
    QList<Video*> movedVideos;

    foreach (QModelIndex index, indexes) {
        int row = index.row();
        if (row >= videos.size()) continue;
        // qDebug() << "index row" << row;
        Video *video = videoAt(row);
        movedVideos << video;
    }

    int end=up ? -1 : rowCount()-1, mod=up ? -1 : 1;
    foreach (Video *video, movedVideos) {

        int row = rowForVideo(video);
        if (row+mod==end) { end=row; continue; }
        // qDebug() << "video row" << row;
        removeRows(row, 1, QModelIndex());

        if (up) row--;
        else row++;

        beginInsertRows(QModelIndex(), row, row);
        videos.insert(row, video);
        endInsertRows();

    }

    emit needSelectionFor(movedVideos);

}

/* row hovering */

void PlaylistModel::setHoveredRow(int row) {
    int oldRow = hoveredRow;
    hoveredRow = row;
    emit dataChanged( createIndex( oldRow, 0 ), createIndex( oldRow, columnCount() - 1 ) );
    emit dataChanged( createIndex( hoveredRow, 0 ), createIndex( hoveredRow, columnCount() - 1 ) );
}

void PlaylistModel::clearHover() {
    emit dataChanged( createIndex( hoveredRow, 0 ), createIndex( hoveredRow, columnCount() - 1 ) );
    hoveredRow = -1;
}

/* clickable author */

void PlaylistModel::enterAuthorHover() {
    if (authorHovered) return;
    authorHovered = true;
    updateAuthor();
}

void PlaylistModel::exitAuthorHover() {
    if (!authorHovered) return;
    authorHovered = false;
    updateAuthor();
    setHoveredRow(hoveredRow);
}

void PlaylistModel::enterAuthorPressed() {
    if (authorPressed) return;
    authorPressed = true;
    updateAuthor();
}

void PlaylistModel::exitAuthorPressed() {
    if (!authorPressed) return;
    authorPressed = false;
    updateAuthor();
}

void PlaylistModel::updateAuthor() {
    emit dataChanged( createIndex( hoveredRow, 0 ), createIndex( hoveredRow, columnCount() - 1 ) );
}