#include "sc_scs_writer.hpp"

#include <regex>

#include <sc-memory/sc_utils.hpp>

#include "sc_scg_element.hpp"
#include "sc_scs_element.hpp"
#include "sc_scg_to_scs_types_converter.hpp"

using namespace Constants;

std::string SCsWriter::MakeAlias(std::string const & prefix, std::string const & elementId)
{
  return ALIAS_PREFIX + prefix + UNDERSCORE + utils::StringUtils::ReplaceAll(elementId, DASH, UNDERSCORE);
}

bool SCsWriter::IsVariable(std::string const & elementType)
{
  return elementType.find(VAR) != std::string::npos;
}

bool SCsWriter::SCgIdentifierCorrector::IsRussianIdentifier(std::string const & identifier)
{
  std::regex identifierPatternRus(R"(^[0-9a-zA-Z_\xD0\x80-\xD1\x8F\xD1\x90-\xD1\x8F\xD1\x91\xD0\x81*' ]*$)");
  return std::regex_match(identifier, identifierPatternRus);
}

bool SCsWriter::SCgIdentifierCorrector::IsEnglishIdentifier(std::string const & identifier)
{
  std::regex identifierPatternEng("^[0-9a-zA-Z_]*$");
  return std::regex_match(identifier, identifierPatternEng);
}

std::string SCsWriter::SCgIdentifierCorrector::GenerateCorrectedIdentifier(
    std::string & systemIdentifier,
    std::string const & elementId,
    bool isVar)
{
  if (systemIdentifier.empty())
    return GenerateIdentifierForUnresolvedCharacters(systemIdentifier, elementId, isVar);

  if (isVar && systemIdentifier[0] != UNDERSCORE[0])
    return GenerateSCsIdentifierForVariable(systemIdentifier);

  return systemIdentifier;
}

std::string SCsWriter::SCgIdentifierCorrector::GenerateIdentifierForUnresolvedCharacters(
    std::string & systemIdentifier,
    std::string const & elementId,
    bool isVar)
{
  std::string const & prefix = isVar ? EL_VAR_PREFIX : EL_PREFIX;
  systemIdentifier = prefix + UNDERSCORE + utils::StringUtils::ReplaceAll(elementId, DASH, UNDERSCORE);
  return systemIdentifier;
}

std::string SCsWriter::SCgIdentifierCorrector::GenerateSCsIdentifierForVariable(std::string & systemIdentifier)
{
  return UNDERSCORE + systemIdentifier;
}

void SCsWriter::SCgIdentifierCorrector::GenerateSCsIdentifier(
    SCgElementPtr const & scgElement,
    SCsElementPtr & scsElement)
{
  scsElement->SetIdentifierForSCs(scgElement->GetIdentifier());
  bool isVar = SCsWriter::IsVariable(scgElement->GetType());

  std::string const & systemIdentifier = scsElement->GetIdentifierForSCs();
  if (!IsEnglishIdentifier(systemIdentifier))
  {
    if (IsRussianIdentifier(systemIdentifier))
      scsElement->SetMainIdentifier(systemIdentifier);

    scsElement->SetIdentifierForSCs("");
  }

  std::string id = scgElement->GetId();
  std::string correctedIdentifier = scsElement->GetIdentifierForSCs();
  correctedIdentifier = GenerateCorrectedIdentifier(correctedIdentifier, id, isVar);
  scsElement->SetIdentifierForSCs(correctedIdentifier);

  auto const & tag = scgElement->GetTag();
  if (tag == PAIR || tag == ARC)
    scsElement->SetIdentifierForSCs(SCsWriter::MakeAlias(CONNECTOR, id));
}

void SCsWriter::Write(
  SCgElements const & elements,
  std::string const & filePath,
  Buffer & buffer,
  size_t depth,
  std::unordered_set<SCgElementPtr> & writtenElements)
{
// Step 1: Write all nodes with their types
for (auto const & [id, element] : elements)
{
  if (element->GetTag() == NODE)
  {
    if (writtenElements.count(element)) continue;
    writtenElements.insert(element);

    std::string identifier = element->GetIdentifier();
    if (identifier.empty())
    {
      identifier = "node_" + element->GetId();
    }
    buffer.AddTabs(depth) << identifier << "\n";
    std::string elementTypeStr;
    SCgToSCsTypesConverter::ConvertSCgNodeTypeToSCsNodeType(element->GetType(), elementTypeStr);
    if (elementTypeStr.empty()) elementTypeStr = "node_";
      buffer.AddTabs(depth + 1) << "<- " << elementTypeStr << ";;\n";
  }
}

// Step 2: Write relationships based on arcs
for (auto const & [id, element] : elements)
{
  if (element->GetTag() == ARC || element->GetTag() == PAIR)
  {
    if (writtenElements.count(element)) continue;
    writtenElements.insert(element);

    auto connector = std::dynamic_pointer_cast<SCgConnector>(element);
    std::string sourceId = connector->GetSource()->GetIdentifier();
    std::string targetId = connector->GetTarget()->GetIdentifier();
    if (sourceId.empty()) sourceId = "node_" + connector->GetSource()->GetId();
    if (targetId.empty()) targetId = "node_" + connector->GetTarget()->GetId();

    std::string relation = connector->GetType();
    if (relation.find("nrel_") == 0 || relation.find("rel_") == 0)
    {
      std::string direction = "=>";
      if (connector->GetSource() != element && connector->GetTarget() == element)
      {
        direction = "<=";
      }
      buffer.AddTabs(depth) << sourceId << " " << direction << " " << relation << ": " << targetId << ";;\n";
    }
    else
    {
      std::string connectorSymbol;
      SCgToSCsTypesConverter::ConvertSCgConnectorTypeToSCsConnectorDesignation(connector->GetType(), connectorSymbol);
      if (connectorSymbol.empty()) connectorSymbol = "->";
      buffer.AddTabs(depth) << sourceId << " " << connectorSymbol << " " << targetId << ";;\n";
    }
  }
}

// Step 3: Write contours as structures
for (auto const & [id, element] : elements)
{
  if (element->GetTag() == CONTOUR)
  {
    if (writtenElements.count(element)) continue;
    writtenElements.insert(element);

    auto contour = std::dynamic_pointer_cast<SCgContour>(element);
    std::string identifier = element->GetIdentifier();
    if (identifier.empty())
    {
      identifier = "contour_" + element->GetId();
    }
    buffer.AddTabs(depth) << identifier << " = [*\n";
    Write(contour->GetElements(), filePath, buffer, depth + 1, writtenElements);
    buffer.AddTabs(depth) << "*];;\n";
  }
  else if (element->GetTag() == BUS)
  {
    if (writtenElements.count(element)) continue;
    writtenElements.insert(element);

    std::string identifier = element->GetIdentifier();
    if (identifier.empty())
    {
      identifier = "bus_" + element->GetId();
    }
    buffer.AddTabs(depth) << identifier << "\n";
    buffer.AddTabs(depth + 1) << "<- " << element->GetType() << ";;\n";
  }
}
}

void SCsWriter::WriteMainIdentifier(
    Buffer & buffer,
    size_t depth,
    std::string const & systemIdentifier,
    std::string const & mainIdentifier)
{
  buffer << NEWLINE;
  buffer.AddTabs(depth) << systemIdentifier << NEWLINE;
  buffer.AddTabs(depth) << SPACE << SC_CONNECTOR_DCOMMON_R << SPACE << NREL_MAIN_IDTF << COLON << SPACE << OPEN_BRACKET
                        << mainIdentifier << CLOSE_BRACKET << ELEMENT_END << NEWLINE;
}