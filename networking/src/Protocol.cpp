#include <pandora/shared/Protocol.h>
#include <QJsonDocument>
#include <QtEndian>
namespace pandora {
static const QStringList names = {
 "CREATE_PANDORA","JOIN_REQUEST","JOIN_ACCEPTED","NAVI_JOINED","NAVI_LEFT",
 "FILE_UPLOAD","FILE_UPDATE","FILE_DELETE","FILE_REQUEST","FILE_RESPONSE",
 "METADATA_SYNC","GUARDIAN_CHANGED","SHUTDOWN_WARNING","PANDORA_DEACTIVATE"};
QString messageTypeName(MessageType type) { return names.value(static_cast<int>(type)); }
QJsonObject Message::toJson() const {
 return {{"protocol",ProtocolVersion},{"type",messageTypeName(type)},{"id",id},
 {"pandoraId",pandoraId},{"senderId",senderId},{"payload",payload}};
}
std::optional<Message> Message::fromJson(const QJsonObject& o, QString* error) {
 int type=names.indexOf(o.value("type").toString());
 if(o.value("protocol").toDouble(-1)!=ProtocolVersion || type<0 ||
 o.value("id").toString().isEmpty() || o.value("senderId").toString().isEmpty() ||
 o.value("pandoraId").toString().isEmpty() || !o.value("payload").isObject()) {
  if(error) *error="Invalid application envelope";
  return std::nullopt;
 }
 Message m; m.type=static_cast<MessageType>(type); m.id=o["id"].toString();
 m.senderId=o["senderId"].toString();m.pandoraId=o["pandoraId"].toString();
 m.payload=o["payload"].toObject();return m;
}
QByteArray encodeFrame(const QJsonObject& o) {
 auto body=QJsonDocument(o).toJson(QJsonDocument::Compact);
 if(body.isEmpty() || body.size()>MaxFrameBytes) return {};
 QByteArray frame(4,'\0');qToBigEndian<quint32>(quint32(body.size()),frame.data());
 return frame+body;
}
bool FrameDecoder::feed(const QByteArray& bytes,QList<QJsonObject>& out,QString& error) {
 for(qsizetype offset=0;offset<bytes.size();) {
  auto take=qMin<qsizetype>(4096,bytes.size()-offset);
  buffer_.append(bytes.constData()+offset,take);offset+=take;
  while(buffer_.size()>=4) {
   auto n=qFromBigEndian<quint32>(buffer_.constData());
   if(n==0 || n>MaxFrameBytes) {error="Invalid frame length";return false;}
   if(buffer_.size()<4+n) break;
   QJsonParseError p;auto doc=QJsonDocument::fromJson(buffer_.mid(4,n),&p);
   if(p.error!=QJsonParseError::NoError || !doc.isObject()) {error="Invalid JSON object";return false;}
   out.append(doc.object());buffer_.remove(0,4+n);
  }
 }
 return true;
}
}

