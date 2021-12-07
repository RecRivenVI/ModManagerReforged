#include "optifineapi.h"

#include <QNetworkReply>
#include <QUrlQuery>

#include "util/funcutil.h"

const QString OptifineAPI::PREFIX = "https://optifine.net";

OptifineAPI::OptifineAPI(QObject *parent) : QObject(parent)
{}

OptifineAPI *OptifineAPI::api()
{
    static OptifineAPI api;
    return &api;
}

QMetaObject::Connection OptifineAPI::getModList(std::function<void (QList<OptifineModInfo>)> callback)
{
    QUrl url = PREFIX + "/downloads";
    QNetworkRequest request(url);
    auto reply = accessManager_.get(request);
    return connect(reply, &QNetworkReply::finished, this, [=]{
        if(reply->error() != QNetworkReply::NoError){
            qDebug() << reply->errorString();
            return;
        }
        auto webPage = reply->readAll();
        reply->deleteLater();

        QRegularExpression re(R"(<tr class='downloadLine.*?'>(.*?)</tr>)");
        //TODO: workaround for multiline match
        webPage.replace('\n', "");
        QList<OptifineModInfo> list;
        GameVersion gameVersion = capture(webPage, "<h2>Minecraft (.+?)</h2>", 0);
        GameVersion nextGameVersion;
        auto it = re.globalMatch(webPage);
        while(it.hasNext()) {
            auto match = it.next();
            if(auto gv = capture(webPage, "<h2>Minecraft (.+?)</h2>", match.capturedStart(1)); gv != nextGameVersion){
                if(nextGameVersion != GameVersion::Any) gameVersion = nextGameVersion;
                nextGameVersion = gv;
            }
            list << OptifineModInfo::fromHtml(match.captured(1), gameVersion);
        }
        callback(list);
    });
}

QMetaObject::Connection OptifineAPI::getDownloadUrl(const QString &fileName, std::function<void (QUrl)> callback)
{
    QUrl url = PREFIX + "/adloadx";
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("f", fileName);
    url.setQuery(urlQuery);
    QNetworkRequest request(url);
    auto reply = accessManager_.get(request);
    return connect(reply, &QNetworkReply::finished, this, [=]{
        if(reply->error() != QNetworkReply::NoError){
            qDebug() << reply->errorString();
            return;
        }
        auto webPage = reply->readAll();
        reply->deleteLater();
        //TODO: workaround for multiline match
        webPage.replace('\n', "");
        auto str = capture(webPage, R"(<a href='(.*?)' onclick='.*?'>.*?</a>)");
        QUrl downloadUrl = "https://optifine.net/" + str;
        callback(downloadUrl);
    });
}
