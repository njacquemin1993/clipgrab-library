/*
    ClipGrab³
    Copyright (C) Philipp Schmieder
    http://clipgrab.de
    feedback [at] clipgrab [dot] de

    This file is part of ClipGrab.
    ClipGrab is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ClipGrab is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ClipGrab.  If not, see <http://www.gnu.org/licenses/>.
*/



#include "video_facebook.h"

video_facebook::video_facebook()
{
    this->_name = "Facebook";
    this->_supportsTitle = true;
    this->_supportsDescription = true;
    this->_supportsThumbnail = true;
    this->_supportsSearch = true;
    this->_icon = 0;
    this->_urlRegExp << QRegExp("(http[s]?://(www\\.)?facebook\\.com.*/videos).*/(\\d+)", Qt::CaseInsensitive);
    this->_urlRegExp << QRegExp("http[s]?://(www\\.)?facebook\\.com.*/(?:pg/)?.*/videos/.*", Qt::CaseInsensitive);
    _treeItem = NULL;
    this->authenticating = false;

    QSettings settings;
    QString serializedCookies = settings.value("facebookCookies", "").toString();
    if (!serializedCookies.isEmpty())
    {
        QList<QNetworkCookie> cookies = this->handler->deserializeCookies(serializedCookies);
        this->handler->networkAccessManager->cookieJar()->setCookiesFromUrl(cookies, QUrl("https://www.facebook.com/"));
    }
}

video* video_facebook::createNewInstance()
{
    return new video_facebook();
}

bool video_facebook::setUrl(QString url)
{
    _originalUrl = url;

    if (_urlRegExp.first().indexIn(url) > -1) {
        this->_url = QUrl(_urlRegExp.first().cap(1) + "/" + _urlRegExp.first().cap(3));
    } else if (_urlRegExp.last().indexIn(url) > -1) {
        this->_url = QUrl(originalUrl());
    }

    if (_url.isValid())
    {
        return true;
    }
    return false;
}


void video_facebook::parseVideo(QString data)
{
    QSettings settings;
    QRegExp expression;

    //Fetch title
    expression = QRegExp("<meta property=\"og:title\" content=\"(.*)\" />");
    expression.setMinimal(true);
    if (expression.indexIn(data) > -1)
    {
        this->_title = expression.cap(1);
    }
    expression = QRegExp("<title id=\"pageTitle\">(.*)</title>");
    expression.setMinimal(true);
    if ((this->_title.isEmpty() || this->_title.startsWith("Facebook") || this->_title.startsWith("Log In")) && expression.indexIn(data) > -1)
    {
        this->_title = expression.cap(1);
    }

    //Fetch userID and videoData
    expression = QRegExp("\"USER_ID\":\"(\\d+)\"");
    QString userID;
    if (expression.indexIn(data) > -1)
    {
        userID = expression.cap(1);
    }
    expression = QRegExp("(\"videoData\":)|videoData:\\[\\{");
    expression.setMinimal(true);
    bool hasVideoData = (expression.indexIn(data) > -1);

    //Determine if login or fetching player is required
    if (userID == "0" && !hasVideoData)
    {
        if (this->authenticating)
        {
            //If we’re already in the process of authenticating
            //and no video data has been found
            return;
        }
        this->authenticating = true;
        dui = new Ui::LoginDialog();
        passwordDialog = new QDialog;
        dui->setupUi(passwordDialog);
        connect(dui->loginDialogWebView, SIGNAL(loadProgress(int)), this, SLOT(handleLogin()));
        connect(this, SIGNAL(analysingFinished()), this, SLOT(acceptLoginDialog()));
        dui->loginDialogWebView->setUrl(QUrl::fromUserInput("https://m.facebook.com/login.php?next=" + QUrl::toPercentEncoding(this->_url.toString())));
        dui->rememberLogin->setChecked(settings.value("facebookRememberLogin", true).toBool());

        if (passwordDialog->exec() == QDialog::Accepted)
        {
            passwordDialog->deleteLater();
            return;
        }

        disconnect(this, SIGNAL(analysingFinished()), this, SLOT(acceptLoginDialog()));
        passwordDialog->deleteLater();
        emit error("Could not retrieve video info.", this);
        emit analysingFinished();
        return;
    }
    else if (userID != "0" && !hasVideoData)
    {
        QRegExp videoIDRegExp = QRegExp("videos/(\\d+)");

        QString dtsg;
        QRegExp dtsgRegExp("name=\"fb_dtsg\" value=\"([^\"]+)\"");
        if (dtsgRegExp.indexIn(data) > -1)
        {
            dtsg = dtsgRegExp.cap(1);
        }
        dtsgRegExp = QRegExp("\\[\"DTSGInitialData\",\\[\\],\\{\"token\":\"([^\"]+)\"");
        if (dtsgRegExp.indexIn(data) > -1)
        {
            dtsg = dtsgRegExp.cap(1);
        }

        if (videoIDRegExp.indexIn(this->_url.toString()) == -1 || dtsg.isEmpty())
        {
            emit analysingFinished();
            return;
        }

        QString playerLink = "https://www.facebook.com/video/tahoe/async/" + videoIDRegExp.cap(1) + "/?payloadtype=primary";

        QUrl postData;
        postData.addQueryItem("__a", "1");
        postData.addQueryItem("__user", userID);
        postData.addQueryItem("fb_dtsg", dtsg);

        qDebug() << "Downloading player ..." << playerLink << postData.encodedQuery();
        handler->addDownload(playerLink, false, postData.encodedQuery());
        return;
    }

    QList< QPair<QRegExp, QString> > supportedQualities;
    supportedQualities.append(qMakePair(QRegExp("\"?hd_src_no_ratelimit\"?:\"([^\"]+)"), tr("HD")));
    supportedQualities.append(qMakePair(QRegExp("\"?hd_src\"?:\"([^\"]+)"), tr("HD")));
    supportedQualities.append(qMakePair(QRegExp("\"?sd_src_no_ratelimit\"?:\"([^\"]+)"), tr("normal")));
    supportedQualities.append(qMakePair(QRegExp("\"?sd_src\"?:\"([^\"]+)"), tr("normal")));

    for (int i = 0; i < supportedQualities.length(); i++)
    {
        QString quality = supportedQualities.at(i).second;
        QRegExp expression = supportedQualities.at(i).first;

        bool isNew = true;
        for (int j = 0; j < this->_supportedQualities.length(); j++) {
            if (this->_supportedQualities.at(j).quality == quality)
            {
                isNew = false;
                break;
            }
        }
        if (!isNew)
        {
            continue;
        }

        if (expression.indexIn(data) == -1)
        {
            continue;
        }
        QString videoLink = expression.cap(1).replace("\\/", "/");
        videoQuality newQuality = videoQuality(quality, videoLink);
        newQuality.containerName = ".mp4";
        if  (newQuality.quality == tr("HD"))
        {
            newQuality.resolution = 720;
        }
        else {
            newQuality.resolution = 360;
        }
        this->_supportedQualities.append(newQuality);
    }


    if (!this->authenticating || (!this->_supportedQualities.isEmpty() && !this->_title.isEmpty())) {
        emit analysingFinished();
    } else {
        emit error("Could not retrieve video info.", this);
    }
}

void video_facebook::handleLogin()
{
    QString html = dui->loginDialogWebView->page()->mainFrame()->toHtml();
    QRegExp userRegexp("\"USER_ID\":\"(\\d+)\"");
    QRegExp dtsgRegexp("\\[\"DTSGInitialData\",\\[\\],\\{\"token\":\"([^\"]+)\"");

    if (dtsgRegexp.indexIn((html)) == -1 || userRegexp.indexIn(html) == -1 || userRegexp.cap(1) == "0") {
        return;
    } else {
        disconnect(dui->loginDialogWebView, SIGNAL(loadProgress(int)), this, SLOT(handleLogin()));
        QList<QNetworkCookie> cookies = dui->loginDialogWebView->page()->networkAccessManager()->cookieJar()->cookiesForUrl(_url);
        handler->networkAccessManager->cookieJar()->setCookiesFromUrl(cookies, _url);
        parseVideo(html);
    }
}

void video_facebook::acceptLoginDialog()
{
    QSettings settings;
    if (dui->rememberLogin->isChecked())
    {
        QList<QNetworkCookie> cookies = dui->loginDialogWebView->page()->networkAccessManager()->cookieJar()->cookiesForUrl(this->_url);
        settings.setValue("facebookCookies", this->handler->serializeCookies(cookies));
    }
    else
    {
        settings.remove("facebookCookies");
    }
    settings.setValue("facebookRememberLogin", dui->rememberLogin->isChecked());
    passwordDialog->accept();
}
