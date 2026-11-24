<?xml version="1.0" encoding="UTF-8"?>
<!--
  t1-21-macro-proxy-tags.xsl - macro and Coupling Amount knobs follow the selected pad (fix plan T1.21, MB-071).
  Identity transform plus:

    1. The six control tags that bound pad 0's per-pad IDs move to the selected-pad proxies (plugin_ids.h):
         CouplingAmount   1036 -> 330 (kCouplingAmountId)
         MacroTightness   1037 -> 331 (kMacroTightnessId)
         MacroBrightness  1038 -> 332 (kMacroBrightnessId)
         MacroBodySize    1039 -> 333 (kMacroBodySizeId)
         MacroPunch       1040 -> 334 (kMacroPunchId)
         MacroComplexity  1041 -> 335 (kMacroComplexityId)
    2. The comment above CouplingAmount describes the proxy.
  The knobs' names, positions and control-tag names stay the same.

  Apply:  npx xslt3 -xsl:specs/membrum-audit/xslt/t1-21-macro-proxy-tags.xsl
                    -s:plugins/membrum/resources/editor.uidesc -o:OUT
  No strip-space and no indent: whitespace text nodes are copied, so only the rewritten values are new. Then restore
  the file's own formatting on OUT (line ends, the attribute layout of the elements the serializer rewrote): the
  rewritten values are the only change to keep.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:map="http://www.w3.org/2005/xpath-functions/map">

  <xsl:output method="xml" encoding="UTF-8" indent="no"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:variable name="retag" as="map(xs:string, xs:string*)"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                select="map {
                  'CouplingAmount'  : ('1036', '330'),
                  'MacroTightness'  : ('1037', '331'),
                  'MacroBrightness' : ('1038', '332'),
                  'MacroBodySize'   : ('1039', '333'),
                  'MacroPunch'      : ('1040', '334'),
                  'MacroComplexity' : ('1041', '335')
                }"/>

  <!-- 1. control tags -->
  <xsl:template match="control-tags/control-tag[map:contains($retag, string(@name))]">
    <xsl:variable name="pair" select="map:get($retag, string(@name))"/>
    <xsl:if test="@tag ne $pair[1]">
      <xsl:message terminate="yes"><xsl:value-of select="@name"/> has tag <xsl:value-of select="@tag"/>, expected <xsl:value-of select="$pair[1]"/></xsl:message>
    </xsl:if>
    <xsl:copy>
      <xsl:copy-of select="@* except @tag"/>
      <xsl:attribute name="tag" select="$pair[2]"/>
    </xsl:copy>
  </xsl:template>

  <!-- 2. the comment above CouplingAmount -->
  <xsl:template match="control-tags/comment()[normalize-space(.) = 'Per-pad Coupling Amount (pad 0 offset 36 = selected-pad proxy)']">
    <xsl:comment> Per-pad Coupling Amount: selected-pad proxy (kCouplingAmountId == 330) for offset 36 </xsl:comment>
  </xsl:template>

</xsl:stylesheet>
