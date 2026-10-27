<?xml version="1.0" encoding="UTF-8"?>
<!--
  Profundum Phase 14 T018 (FR-035; SC-016 class rule; plan section 4.9 / 4.10): the Main-page Morph Position slider
  becomes the display-only MorphIndicatorSlider (a CSlider subclass registered with base view "CSlider").
  Identity transform of plugins/profundum/resources/editor.uidesc (Saxon-JS, xslt3), run after phase14.xsl:
    - the Main-page view class="CSlider" control-tag="MorphPosition" gets class="MorphIndicatorSlider";
      every other attribute is kept in its order. The CParamDisplay bound to MorphPosition is untouched.
  Refuses to run twice (a MorphIndicatorSlider already present) and refuses a source without exactly one
  CSlider bound to MorphPosition.
-->
<xsl:stylesheet version="3.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" encoding="UTF-8" indent="no"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="/">
    <xsl:if test="//view[@class = 'MorphIndicatorSlider']">
      <xsl:message terminate="yes">phase14-indicator.xsl: a MorphIndicatorSlider is already present; the transform was applied before.</xsl:message>
    </xsl:if>
    <xsl:if test="count(//view[@class = 'CSlider'][@control-tag = 'MorphPosition']) != 1">
      <xsl:message terminate="yes">phase14-indicator.xsl: expected exactly one CSlider bound to MorphPosition.</xsl:message>
    </xsl:if>
    <xsl:apply-templates/>
  </xsl:template>

  <!-- The class swap: only the class attribute changes. -->
  <xsl:template match="view[@class = 'CSlider'][@control-tag = 'MorphPosition']/@class">
    <xsl:attribute name="class">MorphIndicatorSlider</xsl:attribute>
  </xsl:template>
</xsl:stylesheet>
