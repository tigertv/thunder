#ifndef PREFABCONVERTER_H
#define PREFABCONVERTER_H

#include "converters/converter.h"

class Actor;

class PrefabConverter : public IConverter {
public:
    QStringList suffixes() const { return {"fab"}; }
    uint32_t                    contentType                 () const { return ContentPrefab; }
    uint32_t                    type                        () const { return MetaType::type<Actor *>(); }
    uint8_t                     convertFile                 (IConverterSettings *);

};

#endif // PREFABCONVERTER_H
