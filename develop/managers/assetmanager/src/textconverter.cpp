#include "textconverter.h"

#include <QFile>

#include <bson.h>
#include "projectmanager.h"

class TextSerial : public Text {
public:
    void                        setData         (const QByteArray &data) {
        m_Data.resize(data.size());
        memcpy(&m_Data[0], data.data(), data.size());
    }

protected:
    VariantMap                  saveUserData    () const {
        VariantMap result;
        result["Data"]  = m_Data;
        return result;
    }
};

uint8_t TextConverter::convertFile(IConverterSettings *s) {
    QFile src(s->source());
    if(src.open(QIODevice::ReadOnly)) {
        TextSerial text;
        text.setData(src.readAll());
        src.close();

        QFile file(ProjectManager::instance()->importPath() + "/" + s->destination());
        if(file.open(QIODevice::WriteOnly)) {
            ByteArray data  = Bson::save( Engine::toVariant(&text) );
            file.write((const char *)&data[0], data.size());
            file.close();
            return 0;
        }
    }

    return 1;
}
